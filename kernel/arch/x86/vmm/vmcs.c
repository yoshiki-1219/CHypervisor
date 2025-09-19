#include <stdint.h>
#include <string.h>
#include "vmcs.h"
#include "msr.h"
#include "paging.h"
#include "log.h"
#include "panic.h"
#include "arch/x86/vmm/vmx_log.h"

static inline int asm_vmclear(uint64_t pa)
{
    uint64_t rflags;
    __asm__ __volatile__(
        "vmclear (%1)\n\t"
        "pushfq\n\t"
        "pop %0\n\t"
        : "=r"(rflags) : "r"(&pa) : "cc","memory");
    return vmx_check_rflags(rflags);
}

static inline int asm_vmptrld(uint64_t pa)
{
    uint64_t rflags;
    __asm__ __volatile__(
        "vmptrld (%1)\n\t"
        "pushfq\n\t"
        "pop %0\n\t"
        : "=r"(rflags) : "r"(&pa) : "cc","memory");
    return vmx_check_rflags(rflags);
}

static inline int asm_vmread(uint64_t field, uint64_t* out_val)
{
    uint64_t rflags;
    __asm__ __volatile__(
        "vmread %2, %1\n\t"
        "pushfq\n\t"
        "pop %0\n\t"
        : "=&r"(rflags),
          "=m"(*out_val)
        : "r"(field)
        : "cc", "memory");
    return vmx_check_rflags(rflags);
}

static inline int asm_vmwrite(uint64_t field, uint64_t val)
{
    uint64_t rflags;
    __asm__ __volatile__(
        "vmwrite %1, %2\n\t"
        "pushfq\n\t"
        "pop %0\n\t"
        : "=r"(rflags)
        : "r"(val), "r"(field)
        : "cc","memory");
    return vmx_check_rflags(rflags);
}

/* ================= VMREAD / VMWRITE ラッパ ================== */

int vmcs_vmwrite(uint64_t field, uint64_t value)
{
    int rc = asm_vmwrite(field, value);
    if (rc != 0) {
        uint32_t err = vmcs_instruction_error();
        KLOG_ERROR("vmcs", "VMWRITE field=0x%016llX val=0x%llX -> err=%u",
                   (unsigned long long)field, (unsigned long long)value, err);
        return -1;
    }
    return 0;
}

int vmcs_vmread(uint64_t field, uint64_t* out_value)
{
    uint64_t v = 0;
    int rc = asm_vmread(field, &v);
    if (rc != 0) {
        uint32_t err = vmcs_instruction_error();
        KLOG_ERROR("vmcs", "VMREAD field=0x%llX -> err=%u",
                   (unsigned long long)field, err);
        return -1;
    }
    if (out_value) *out_value = v;
    return 0;
}

/* ================= VMCS Region の確保とロード =============== */

static void*    s_vmcs_va  = 0;
static uint64_t s_vmcs_pa  = 0;

int vmcs_alloc_and_load(void** out_vmcs_va)
{
    /* 1) 4KiB ページ確保＆クリア */
    s_vmcs_va = page_alloc_4k_aligned();
    if (!s_vmcs_va) {
        KLOG_ERROR("vmcs", "alloc failed");
        return -1;
    }
    memset(s_vmcs_va, 0, 4096);

    /* 2) revision ID を先頭 4 バイトへ */
    uint64_t basic = rdmsr(IA32_VMX_BASIC);
    uint32_t rev   = (uint32_t)(basic & 0x7FFFFFFFu);
    *(uint32_t*)s_vmcs_va = rev;

    /* 3) 物理アドレス取得 */
    s_vmcs_pa = virt2phys((uint64_t)s_vmcs_va);

    /* 4) VMCLEAR → VMPTRLD で “Current & Clear & Active” に */
    if (asm_vmclear(s_vmcs_pa) != 0) {
        vmcs_log_error("VMCLEAR failed");
        return -1;
    }
    if (asm_vmptrld(s_vmcs_pa) != 0) {
        vmcs_log_error("VMPTRLD failed");
        return -1;
    }

    if (out_vmcs_va) *out_vmcs_va = s_vmcs_va;
    KLOG_INFO("vmcs", "VMCS loaded: va=%p pa=0x%llX rev=0x%llX",
              s_vmcs_va, (unsigned long long)s_vmcs_pa, (unsigned)rev);
    return 0;
}