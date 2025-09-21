#include <string.h>
#include "arch/x86/vmm/ept.h"
#include "arch/x86/vmm/vmcs.h"
#include "arch/x86/vmm/vmx.h"
#include "arch/x86/vmm/vcpu.h"
#include "arch/x86/msr.h"
#include "page_alloc.h"
#include "log.h"
#include "arch/x86/paging.h"

/* ===== 内部ユーティリティ ===== */
static inline int set_eptp(eptp_t eptp)
{
    if (vmcs_vmwrite(VMCS_EPTP, (uint64_t)eptp.u64) != 0) {
        return -1;
    }
    return 0;
}

static ept_table_t* ept_table_alloc(void)
{
    ept_table_t* t = (ept_table_t*)page_alloc_4k_zero();
    return t;
}

static inline ept_entry_t* ept_get_or_create_lv2(ept_table_t* pml4, uint64_t gpa)
{
    /* Lv4 */
    uint64_t i4 = (gpa >> EPT_LV4_SHIFT) & EPT_IDX_MASK;
    ept_entry_t* pml4e = &pml4->e[i4];
    if (!(pml4e->f.r | pml4e->f.w | pml4e->f.xs)) {
        ept_table_t* new_tbl = ept_table_alloc(); //get Lv3
        if (!new_tbl) return NULL;
        pml4e->u64 = 0;
        pml4e->f.r = pml4e->f.w = pml4e->f.xs = 1;
        pml4e->f.mt = EPT_TYPE_UC;
        pml4e->f.super = 0;
        pml4e->f.phys = virt2phys((uint64_t)new_tbl) >> 12;
    }
    ept_table_t* pdpt = (ept_table_t*)phys2virt(pml4e->f.phys << 12);

    /* Lv3 */
    uint64_t i3 = (gpa >> EPT_LV3_SHIFT) & EPT_IDX_MASK;
    ept_entry_t* pdpte = &pdpt->e[i3];
    if (!(pdpte->f.r | pdpte->f.w | pdpte->f.xs)) {
        ept_table_t* new_tbl = ept_table_alloc(); //get Lv2
        if (!new_tbl) return NULL;
        pdpte->u64 = 0;
        pdpte->f.r = pdpte->f.w = pdpte->f.xs = 1;
        pdpte->f.mt = EPT_TYPE_UC;
        pdpte->f.super = 0;
        pdpte->f.phys = virt2phys((uint64_t)new_tbl) >> 12;
    }
    ept_table_t* pde_tbl = (ept_table_t*)phys2virt(pdpte->f.phys << 12);

    /* Lv2 (2MiB 大ページ) のエントリを返す */
    return &pde_tbl->e[(gpa >> EPT_LV2_SHIFT) & EPT_IDX_MASK];
}

static int ept_map_2m(ept_table_t* pml4, uint64_t gpa_2m, uint64_t hpa_2m)
{
    if ((gpa_2m & (PAGE_SIZE_2M - 1)) || (hpa_2m & (PAGE_SIZE_2M - 1)))
        return -1;

    ept_entry_t* pde = ept_get_or_create_lv2(pml4, gpa_2m);
    if (!pde) return -2;
    if (pde->f.r | pde->f.w | pde->f.xs) return -3; /* already mapped */

    pde->u64 = 0;
    pde->f.r = pde->f.w = pde->f.xs = 1;
    pde->f.mt = 0;           /* 大ページ時 type フィールドは 0 に（ReservedZ 相当） */
    pde->f.super = 1;        /* 2MiB page */
    pde->f.phys  = (hpa_2m >> 12); /* 下位 21bit は 0 想定 */
    return 0;
}

/* ===== 公開: EPT 構築 ===== */
int ept_build_identity_2m(uint64_t guest_start_gpa,
                          uint64_t host_start_hpa,
                          size_t   size_bytes,
                          eptp_t*  out_eptp)
{
    if (!out_eptp) return -1;
    if ((guest_start_gpa & (PAGE_SIZE_2M-1)) || (host_start_hpa & (PAGE_SIZE_2M-1)))
        return -2;

    ept_table_t* pml4 = ept_table_alloc();
    
    if (!pml4) return -3;

    size_t n2m = size_bytes / PAGE_SIZE_2M;
    for (size_t i = 0; i < n2m; ++i) {
        uint64_t gpa = guest_start_gpa + i * PAGE_SIZE_2M;
        uint64_t hpa = host_start_hpa  + i * PAGE_SIZE_2M;
        int rc = ept_map_2m(pml4, gpa, hpa);
        if (rc != 0) return rc;
    }

    eptp_t eptp = { .u64 = 0 };
    eptp.f.type = EPT_TYPE_WB;
    eptp.f.pwl  = 3;                 /* 4-level → 3 */
    eptp.f.ad   = 1;                 /* A/D */
    eptp.f.pml4 = virt2phys((uint64_t)pml4) >> 12;

    *out_eptp = eptp;
    return 0;
}

/* ===== ゲスト領域の確保（2MiB アライン）＋ EPT 恒等マップ作成 =====
guest_vpa ゲストのために確保した領域のホストの論理アドレス
guest_hpa ゲストのために確保した領域のホストの物理アドレス
 */
int ept_prepare_guest_region(uint64_t* out_guest_hva,
                             uint64_t* out_guest_hpa,
                             size_t guest_bytes,
                             eptp_t* out_eptp)
{
    if (!out_guest_hva || !out_guest_hpa || !out_eptp) return -1;
    if (guest_bytes == 0) return -2;

    size_t need_2m = (guest_bytes + PAGE_SIZE_2M - 1) / PAGE_SIZE_2M;
    void* hva = page_alloc_pages(need_2m, PAGE_2M_ALIGN);
    
    if (!hva) return -3;

    uint64_t hpa = virt2phys((uint64_t)hva);
    /* ゲスト GPA は 0 始まりで割り当てる */
    int rc = ept_build_identity_2m(/*gpa*/0, /*hpa*/hpa, need_2m * PAGE_SIZE_2M, out_eptp);
    if (rc != 0) return rc;

    *out_guest_hva = (uint64_t)hva;
    *out_guest_hpa = (uint64_t)hpa;
    return 0;
}

// /* ===== VMCS: EPT/Unrestricted/VPID を有効化し、EPTP/VPID をセット ===== */
// int vmx_enable_ept_unrestricted_vpid(eptp_t eptp, uint16_t vpid)
// {
//     /* EPTP を書く（64-bit） */
//     if (vmcs_vmwrite(VMCS_EPTP, eptp.u64) != 0) return -1;

//     /* Secondary: EPT(bit1) + Unrestricted Guest(bit7) + VPID(bit5 可能なら) */
//     uint64_t sec_msr = rdmsr(IA32_VMX_PROCBASED_CTLS2);
//     uint32_t sec = 0;
//     const uint32_t SEC_EPT          = (1u << 1);
//     const uint32_t SEC_VPID         = (1u << 5);
//     const uint32_t SEC_UNRESTRICTED = (1u << 7);
//     sec |= SEC_EPT | SEC_UNRESTRICTED;

//     /* 簡易: VPID 対応 MSR（IA32_VMX_EPT_VPID_CAP）の INVVPID サポート有無で判断 */
//     uint64_t cap = rdmsr(IA32_VMX_EPT_VPID_CAP);
//     if (cap) sec |= SEC_VPID; /* 厳密には各 bit を見る。最小実装では “cap!=0 なら可” とする */

//     sec = adjust_ctrl_u32(sec, sec_msr);
//     if (vmcs_vmwrite(VMCS_SECONDARY_PROC_CTLS, sec) != 0) return -3;

//     if (sec & SEC_VPID) {
//         if (vpid == 0) vpid = 1; /* 0 はホスト予約 */
//         if (vmcs_vmwrite(VMCS_VPID, vpid) != 0) return -4;
//     }

//     /* Entry: IA-32e Guest を無効化（Unrestricted + paging off で入るため） */
//     uint64_t entry_msr = rdmsr(IA32_VMX_TRUE_ENTRY_CTLS);
//     uint32_t entry = 0; /* ia32e_mode_guest(bit9)=0, load_ia32_efer(bit15)=0 */
//     entry = adjust_ctrl_u32(entry, entry_msr);
//     if (vmcs_vmwrite(VMCS_VMENTRY_CTLS, entry) != 0) return -5;

//     /* Exit: host_addr_space_size(bit9)=1（ホストが 64bit の場合）。必要なら EFER load も */
//     uint64_t exit_msr = rdmsr(IA32_VMX_TRUE_EXIT_CTLS);
//     uint32_t exitc = (1u << 9); /* host_addr_space_size */
//     exitc = adjust_ctrl_u32(exitc, exit_msr);
//     if (vmcs_vmwrite(VMCS_VMEXIT_CTLS, exitc) != 0) return -6;

//     return 0;
// }

/* VMX root operation 突入後、VMLAUNCH 前の初期化で呼ぶ */
int vcpu_setup_ept(Vcpu* vcpu)
{
    /* 1) ゲスト用の連続メモリを 2MiB アラインで 100MiB 確保 → EPT 構築 */
    const size_t guest_bytes = 100ULL * 1024 * 1024;
    uint64_t guest_hva = 0;
    uint64_t guest_hpa = 0;
    eptp_t eptp;

    if (ept_prepare_guest_region(&guest_hva, &guest_hpa, guest_bytes, &eptp) != 0) {
        KLOG_ERROR("ept", "prepare_guest_region failed");
        return -1;
    }
    KLOG_INFO("vmx", "Guest memory mapped: HVA=%p HPA=0x%llx size=0x%zx",
              guest_hva, guest_hpa, guest_bytes);

    /* 2) VMCS に EPTP をセット */
    if (set_eptp(eptp) != 0) {
        KLOG_ERROR("vmx", "EPTP setting failed");
        return -1;
    }

    vcpu->guest_base = guest_hva;
    vcpu->eptp       = eptp;

    return 0;
}
