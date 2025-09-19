#include <stdint.h>
#include <string.h>
#include "cpuid.h"
#include "msr.h"
#include "paging.h"
#include "panic.h"
#include "log.h"
#include "common.h"
#include "arch/x86/arch_x86_low.h"
#include "arch/x86/vmm/vmx_log.h"

/* ---------- VMX 命令ラッパ（RFLAGS を成否で返す） ---------- */

static inline int asm_vmxon(uint64_t pa)
{
    uint64_t rflags;
    __asm__ __volatile__(
        "vmxon (%1)\n\t"
        "pushfq\n\t"
        "pop %0\n\t"
        : "=r"(rflags) : "r"(&pa) : "cc","memory");
    return vmx_check_rflags(rflags);
}

static inline int asm_vmxoff(void)
{
    uint64_t rflags;
    __asm__ __volatile__(
        "vmxoff\n\t"
        "pushfq\n\t"
        "pop %0\n\t"
        : "=r"(rflags) :: "cc","memory");
    return vmx_check_rflags(rflags);
}

static void vmx_adjust_cr0_cr4(void)
{
    uint32_t f0 = vmx_cr0_fixed0();
    uint32_t f1 = vmx_cr0_fixed1();
    uint64_t cr0 = read_cr0();
    cr0 |= f0;      /* must be 1 */
    cr0 &= f1;      /* must be 0 の位置は 0 に落ちる */
    write_cr0(cr0);

    f0 = vmx_cr4_fixed0();
    f1 = vmx_cr4_fixed1();
    uint64_t cr4 = read_cr4();
    cr4 |= f0;
    cr4 &= f1;
    /* CR4.VMXE(bit13) を必ず 1 に */
    cr4 |= (1ULL<<13);
    write_cr4(cr4);
}

/* ---------- FEATURE_CONTROL の確認/設定 ---------- */

static int vmx_enable_feature_control_outside_smx(void)
{
    ia32_feature_control_t fc; fc.u64 = rdmsr(IA32_FEATURE_CONTROL);

    if (fc.lock) {
        /* 既にロック済み。outside_smx が有効ならOK、無効なら不可。 */
        return fc.vmx_outside_smx ? 0 : -1;
    } else {
        /* ロック前：outside_smx を有効化してロックする */
        fc.vmx_outside_smx = 1;
        fc.lock = 1;
        wrmsr(IA32_FEATURE_CONTROL, fc.u64);
        /* 読み戻し確認 */
        fc.u64 = rdmsr(IA32_FEATURE_CONTROL);
        return (fc.lock && fc.vmx_outside_smx) ? 0 : -1;
    }
}

/* ---------- VMXON Region の確保 ---------- */

static void*   s_vmxon_va   = NULL;
static uint64_t s_vmxon_pa  = 0;

static int vmx_alloc_and_init_vmxon_region(void)
{
    /* 4KiB アライン 1ページ確保（実装依存サイズだが通常 4KiB） */
    s_vmxon_va = page_alloc_4k_aligned();
    if (!s_vmxon_va) return -1;
    memset(s_vmxon_va, 0, 4096);

    ia32_vmx_basic_t basic; basic.u64 = rdmsr(IA32_VMX_BASIC);
    /* 先頭 4 バイトに VMCS revision ID を書く */
    *(uint32_t*)s_vmxon_va = (uint32_t)basic.vmcs_revision_id;

    s_vmxon_pa = virt2phys((uint64_t)s_vmxon_va);
    return 0;
}

/* ---------- 公開 API：VMX Root Operation へ入る ---------- */

int vmx_init_and_enter(void)
{
    /* 1) Vendor/機能チェック */
    char vendor[12];
    cpuid_get_vendor(vendor);
    if (memcmp(vendor, "GenuineIntel", 12) != 0) { 
        KLOG_ERROR("vmx", "CPUID: CPU is not GenuineIntel");
        return -1; 
    }
    KLOG_DEBUG("vmx", "CPUID: CPU is GenuineIntel");

    if (!cpuid_has_vmx()) {
        KLOG_ERROR("vmx", "CPUID: VMX not supported");
        return -1;
    }
    KLOG_DEBUG("vmx", "CPUID: VMX supported");

    /* 2) FEATURE_CONTROL により VMX outside SMX を許可し、ロック */
    if (vmx_enable_feature_control_outside_smx() != 0) {
        KLOG_ERROR("vmx", "IA32_FEATURE_CONTROL: VMX outside SMX disabled or locked");
        return -1;
    }

    /* 3) CR0/CR4 を fixed MSR に合わせ、CR4.VMXE=1 */
    vmx_adjust_cr0_cr4();

    /* 4) VMXON Region 準備（先頭に revision_id 設定） */
    if (vmx_alloc_and_init_vmxon_region() != 0) {
        KLOG_ERROR("vmx", "Failed to allocate VMXON region");
        return -1;
    }

    /* 5) VMXON 実行（物理アドレスを間接オペランドで渡す） */
    if (asm_vmxon(s_vmxon_pa) != 0) {
        KLOG_ERROR("vmx", "VMXON failed");
        return -1;
    }

    KLOG_INFO("vmx", "Entered VMX Root Operation (VMXON ok)");
    return 0;
}

/* 必要になったら VMXOFF 実装 */
int vmx_leave(void)
{
    int rc = asm_vmxoff();
    if (rc != 0) {
        KLOG_ERROR("vmx", "VMXOFF failed");
        return -1;
    }
    KLOG_INFO("vmx", "Left VMX Operation");
    return 0;
}
