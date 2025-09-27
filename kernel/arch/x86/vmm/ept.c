#include <stdint.h>
#include "log.h"
#include "common.h"
#include "arch/x86/paging.h"
#include "arch/x86/vmm/vmx_vpid.h"
#include "arch/x86/vmm/vcpu.h"
#include "arch/x86/vmm/ept.h"
#include "page_alloc.h"

static inline bool present(const EptEntry* ent) {
    return ent->f.read || ent->f.write || ent->f.exec_super || ent->f.exec_user;
}

static inline Phys address_of(const EptEntry* ent) {
    return (Phys)ent->f.phys << page_shift_4k;
}

static inline EptTable* table_from_phys(Phys paddr) {
    return (EptTable*)(uintptr_t)phys2virt(paddr);
}

/* EntryBase.newMapTable(table) 相当（Lv1 は不許可：Zig と同様の前提） */
static inline EptEntry newMapTable(EptTable* lower_tbl) {
    EptEntry e = { .u64 = 0 };
    e.f.read       = 1;
    e.f.write      = 1;
    e.f.exec_super = 1;
    e.f.exec_user  = 1;
    e.f.map_memory = 0;
    e.f.type       = MemoryType_Uncacheable; /* Zig: テーブル参照時は UC を入れている */
    e.f.phys       = (virt2phys((uint64_t)(uintptr_t)lower_tbl)) >> page_shift_4k;
    return e;
}

/* EntryBase.newMapPage(phys) 相当（Lv4 では不許可：呼び出し側が Lv2/Lv1 で使う） */
static inline EptEntry newMapPage(Phys phys) {
    EptEntry e = { .u64 = 0 };
    e.f.read       = 1;
    e.f.write      = 1;
    e.f.exec_super = 1;
    e.f.exec_user  = 1;
    e.f.map_memory = 1;
    e.f.type       = 0; /* ReservedZ */
    e.f.phys       = (uint64_t)(phys >> page_shift_4k);
    return e;
}

/* ================= テーブル取得（Zig: getTable / getEntry 群） ================= */

static inline EptTable* getTable_from_phys(Phys table_phys) {
    return table_from_phys(table_phys);
}

static inline EptEntry* getEntry(EptTable* tbl, Phys gpa, int level_shift) {
    size_t idx = (size_t)((gpa >> level_shift) & index_mask);
    return &tbl->e[idx];
}

static inline Lv3Entry* getLv3Entry(Phys gpa, Phys lv3tbl_paddr) {
    return (Lv3Entry*)getEntry(getTable_from_phys(lv3tbl_paddr), gpa, lv3_shift);
}
static inline Lv2Entry* getLv2Entry(Phys gpa, Phys lv2tbl_paddr) {
    return (Lv2Entry*)getEntry(getTable_from_phys(lv2tbl_paddr), gpa, lv2_shift);
}
static inline Lv1Entry* getLv1Entry(Phys gpa, Phys lv1tbl_paddr) {
    return (Lv1Entry*)getEntry(getTable_from_phys(lv1tbl_paddr), gpa, lv1_shift);
}

/* ================= initTable (Zig) =================
   Zig では allocator.alloc(T, 512) 後に各フィールドを non-present へ。
   ここでは 4KiB ゼロページを要求し、ゼロ=non-present とする。
*/
static inline EptTable* initTable() {
    EptTable* t = (EptTable*)page_alloc_4k_zero();
    return t; /* ゼロ初期化済み → 全エントリ non-present */
}

/* ================= map2m (Zig) ================= */
static ept_error_t map2m(Phys gpa, Phys hpa, Lv4Entry* lv4tbl) {
    if ((gpa & page_mask_2mb) || (hpa & page_mask_2mb)) return EptErrInvalidArg;

    /* Lv4 */
    Lv4Entry* lv4ent = (Lv4Entry*)&lv4tbl[(gpa >> lv4_shift) & index_mask];
    if (!present(lv4ent)) {
        EptTable* lv3tbl = initTable();
        if (!lv3tbl) return EptErrOutOfMemory;
        *lv4ent = newMapTable(lv3tbl);
    }

    /* Lv3 */
    Lv3Entry* lv3ent = getLv3Entry(gpa, address_of(lv4ent));
    if (!present(lv3ent)) {
        EptTable* lv2tbl = initTable();
        if (!lv2tbl) return EptErrOutOfMemory;
        *lv3ent = newMapTable(lv2tbl);
    }
    if (lv3ent->f.map_memory) return EptErrAlreadyMapped;

    /* Lv2 (2MiB 大ページ) */
    Lv2Entry* lv2ent = getLv2Entry(gpa, address_of(lv3ent));
    if (present(lv2ent)) return EptErrAlreadyMapped;
    *lv2ent = newMapPage(hpa);
    return EptOk;
}

/* ================= translate (Zig) ================= */
bool translate_gpa_to_hpa(Phys guest_gpa, Lv4Entry* lv4tbl, Phys* out_hpa) {
    if (!lv4tbl || !out_hpa) return false;

    /* Lv4 */
    size_t lv4index = (guest_gpa >> lv4_shift) & index_mask;
    Lv4Entry lv4ent = lv4tbl[lv4index];
    if (!present(&lv4ent)) return false;

    /* Lv3 */
    Lv3Entry* p_lv3ent = getLv3Entry(guest_gpa, address_of(&lv4ent));
    Lv3Entry  lv3ent   = *p_lv3ent;
    if (!present(&lv3ent)) return false;
    if (lv3ent.f.map_memory) {
        *out_hpa = address_of(&lv3ent) + (guest_gpa & page_mask_1gb);
        return true;
    }

    /* Lv2 */
    Lv2Entry* p_lv2ent = getLv2Entry(guest_gpa, address_of(&lv3ent));
    Lv2Entry  lv2ent   = *p_lv2ent;
    if (!present(&lv2ent)) return false;
    if (lv2ent.f.map_memory) {
        *out_hpa = address_of(&lv2ent) + (guest_gpa & page_mask_2mb);
        return true;
    }

    /* Lv1 */
    Lv1Entry* p_lv1ent = getLv1Entry(guest_gpa, address_of(&lv2ent));
    Lv1Entry  lv1ent   = *p_lv1ent;
    if (!present(&lv1ent)) return false;
    *out_hpa = address_of(&lv1ent) + (guest_gpa & page_mask_4k);
    return true;
}

/* ================= Eptp.new / Eptp.getLv4 (Zig) ================= */

Eptp eptp_new(Lv4Entry* lv4tbl) {
    Eptp p = { .u64 = 0 };
    p.f.type      = MemoryType_WriteBack; /* Zig: .write_back */
    p.f.level     = PageLevel_Four;       /* four = 3 */
    p.f.enable_ad = 1;                    /* 環境がサポートする前提。必要なら MSR で分岐 */
    p.f.enable_ar = 0;
    p.f.phys      = (virt2phys((uint64_t)(uintptr_t)lv4tbl)) >> page_shift_4k;
    return p;
}

Lv4Entry* eptp_getLv4(Eptp* eptp) {
    Phys pa = (Phys)eptp->f.phys << page_shift_4k;
    return (Lv4Entry*)(uintptr_t)phys2virt(pa);
}

/* ================= initEpt (Zig) ================= */

ept_error_t initEpt(Phys guest_start,
                    Phys host_start,
                    size_t size,
                    Eptp* out_eptp)
{
    if (!out_eptp) return EptErrInvalidArg;

    /* 2MiB アライン必須（Zig と同様） */
    if ((guest_start & page_mask_2mb) ||
        (host_start  & page_mask_2mb) ||
        (size        & page_mask_2mb))
        return EptErrInvalidArg;

    /* Zig: if (size > page_size_1gb * num_table_entries) @panic("too large") */
    const uint64_t max_bytes = (uint64_t)page_size_1g * (uint64_t)num_table_entries; /* 512 GiB */
    if (size > max_bytes) return EptErrTooLarge;

    /* Lv4 テーブル確保 */
    Lv4Entry* lv4tbl = (Lv4Entry*)initTable();
    if (!lv4tbl) return EptErrOutOfMemory;

    /* 2MiB 単位で map2m */
    size_t n2m = size / page_size_2m;
    for (size_t i = 0; i < n2m; ++i) {
        Phys gpa = guest_start + (Phys)i * page_size_2m;
        Phys hpa = host_start  + (Phys)i * page_size_2m;
        ept_error_t rc = map2m(gpa, hpa, lv4tbl);
        if (rc != EptOk) return rc;
    }

    *out_eptp = eptp_new(lv4tbl);
    return EptOk;
}

/* VMX root operation 突入後、VMLAUNCH 前の初期化で呼ぶ */
int vcpu_setup_ept(Vcpu* vcpu)
{
    if (!vcpu) return -1;

    /* 1) ゲスト用の連続メモリを 2MiB アラインで 100MiB 確保 → GPA=0 恒等マップの EPT を構築 */
    const uint64_t guest_bytes = 100ULL * 1024 * 1024;
    const uint64_t need_2m     = (guest_bytes + (page_size_2m - 1)) / page_size_2m;

    void* guest_hva = page_alloc_pages(need_2m, page_size_2m); /* 2MiB アライン確保 */
    if (!guest_hva) {
        KLOG_ERROR("ept", "guest memory allocation failed");
        return -1;
    }
    uint64_t guest_hpa = virt2phys((uint64_t)(uintptr_t)guest_hva);

    Eptp eptp;
    ept_error_t rc = initEpt(/*guest_start=*/0,
                             /*host_start=*/(Phys)guest_hpa,
                             /*size=*/need_2m * page_size_2m,
                             /*out*/ &eptp);
    if (rc != EptOk) {
        KLOG_ERROR("ept", "initEpt failed: %d", rc);
        return -1;
    }

    KLOG_INFO("vmx", "Guest memory mapped: HVA=%p HPA=0x%llx size=0x%zx",
              guest_hva, (unsigned long long)guest_hpa, need_2m * page_size_2m);

    /* 2) VMCS に EPTP をセット */
    if (vmcs_vmwrite(VMCS_EPTP, (uint64_t)eptp.u64) != 0) {
        KLOG_ERROR("vmx", "vmwrite(EPTP) failed");
        return -1;
    }

    /* 4) vCPU 構造体へ保存（のちの参照用） */
    vcpu->guest_base = (uint64_t)(uintptr_t)guest_hva; /* HVA（Zig の guest_mem.ptr 相当） */
    vcpu->eptp       = eptp;                           /* Zig の Vcpu.eptp と同様 */

    return 0;
}