#pragma once
#include <stdint.h>
#include <stddef.h>

static inline int vmx_check_rflags(uint64_t rflags) {
    const uint64_t CF = 1ull << 0;
    const uint64_t ZF = 1ull << 6;
    return ((rflags & (CF | ZF)) != 0) ? -1 : 0;
}

uint32_t vmcs_instruction_error(void);  /* 失敗時は 0（= not_available） */
void     vmcs_log_error(const char* ctx);
const char* vmx_error_to_string(uint32_t err);

enum vmx_instruction_error {
    vmxerr_ok__not_available   = 0,
    vmxerr_vmcall_in_vmxroot   = 1,
    vmxerr_vmclear_invalid_phys= 2,
    vmxerr_vmclear_vmxonptr    = 3,
    vmxerr_vmlaunch_nonclear   = 4,
    vmxerr_vmresume_nonlaunch  = 5,
    vmxerr_vmresume_after_off  = 6,
    vmxerr_vmentry_bad_ctrl    = 7,
    vmxerr_vmentry_bad_host    = 8,
    vmxerr_vmptrld_invalid     = 9,
    vmxerr_vmptrld_vmxonptr    = 10,
    vmxerr_vmptrld_bad_rev     = 11,
    vmxerr_vmrw_unsupported    = 12,
    vmxerr_vmwrite_to_readonly = 13,
    vmxerr_vmxon_in_vmxroot    = 15,
    vmxerr_vmentry_bad_execctl = 16,
    vmxerr_vmentry_nonlaunch_execctl = 17,
    vmxerr_vmentry_exec_vmcsptr= 18,
    vmxerr_vmcall_nonclear     = 19,
    vmxerr_vmcall_invalid_exitctl = 20,
    vmxerr_vmcall_incorrect_msgrev = 22,
    vmxerr_vmxoff_dualmonitor  = 23,
    vmxerr_vmcall_invalid_smm  = 24,
    vmxerr_vmentry_events_blocked = 26,
    vmxerr_invalid_invept      = 28,
};

/* ===========================================================
 * VM-exit Reasons (cf. SDM Vol.3C 25.9)
 * =========================================================== */

enum vm_exit_reason {
    VMX_EXIT_EXCEPTION_NMI          = 0,
    VMX_EXIT_EXTERNAL_INTERRUPT     = 1,
    VMX_EXIT_TRIPLE_FAULT           = 2,
    VMX_EXIT_INIT_SIGNAL            = 3,
    VMX_EXIT_SIPI                   = 4,
    VMX_EXIT_IO_SMI                 = 5,
    VMX_EXIT_OTHER_SMI              = 6,
    VMX_EXIT_INTR_WINDOW            = 7,
    VMX_EXIT_NMI_WINDOW             = 8,
    VMX_EXIT_TASK_SWITCH            = 9,
    VMX_EXIT_CPUID                  = 10,
    VMX_EXIT_GETSEC                 = 11,
    VMX_EXIT_HLT                    = 12,
    VMX_EXIT_INVD                   = 13,
    VMX_EXIT_INVLPG                 = 14,
    VMX_EXIT_RDPMC                  = 15,
    VMX_EXIT_RDTSC                  = 16,
    VMX_EXIT_RSM                    = 17,
    VMX_EXIT_VMCALL                 = 18,
    VMX_EXIT_VMCLEAR                = 19,
    VMX_EXIT_VMLAUNCH               = 20,
    VMX_EXIT_VMPTRLD                = 21,
    VMX_EXIT_VMPTRST                = 22,
    VMX_EXIT_VMREAD                 = 23,
    VMX_EXIT_VMRESUME               = 24,
    VMX_EXIT_VMWRITE                = 25,
    VMX_EXIT_VMXOFF                 = 26,
    VMX_EXIT_VMXON                  = 27,
    VMX_EXIT_CR_ACCESS              = 28,
    VMX_EXIT_DR_ACCESS              = 29,
    VMX_EXIT_IO_INSTRUCTION         = 30,
    VMX_EXIT_RDMSR                  = 31,
    VMX_EXIT_WRMSR                  = 32,
    VMX_EXIT_INVALID_GUEST_STATE    = 33,
    VMX_EXIT_MSR_LOADING            = 34,
    VMX_EXIT_MWAIT_INSTRUCTION      = 36,
    VMX_EXIT_MONITOR_TRAP_FLAG      = 37,
    VMX_EXIT_MONITOR_INSTRUCTION    = 39,
    VMX_EXIT_PAUSE_INSTRUCTION      = 40,
    VMX_EXIT_MCE_DURING_ENTRY       = 41,
    VMX_EXIT_TPR_BELOW_THRESHOLD    = 43,
    VMX_EXIT_APIC_ACCESS            = 44,
    VMX_EXIT_VIRTUALIZED_EOI        = 45,
    VMX_EXIT_GDTR_IDTR_ACCESS       = 46,
    VMX_EXIT_LDTR_TR_ACCESS         = 47,
    VMX_EXIT_EPT_VIOLATION          = 48,
    VMX_EXIT_EPT_MISCONFIG          = 49,
    VMX_EXIT_INVEPT                 = 50,
    VMX_EXIT_RDTSCP                 = 51,
    VMX_EXIT_VMX_PREEMPTION_TIMER   = 52,
    VMX_EXIT_INVVPID                = 53,
    VMX_EXIT_WBINVD                 = 54,
    VMX_EXIT_XSETBV                 = 55,
    VMX_EXIT_APIC_WRITE             = 56,
    VMX_EXIT_RDRAND                 = 57,
    VMX_EXIT_INVPCID                = 58,
    VMX_EXIT_VMFUNC                 = 59,
    VMX_EXIT_ENCLS                  = 60,
    VMX_EXIT_RDSEED                 = 61,
    VMX_EXIT_PMLOG_FULL             = 62,
    VMX_EXIT_XSAVES                 = 63,
    VMX_EXIT_XRSTORS                = 64,
    VMX_EXIT_SPP_EVENT              = 66,
    VMX_EXIT_UMWAIT                 = 67,
    VMX_EXIT_TPAUSE                 = 68,
    VMX_EXIT_BUS_LOCK               = 74,
    VMX_EXIT_NOTIFY                 = 75,
};