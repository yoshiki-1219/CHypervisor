#include <stdint.h>
#include <string.h>
#include "vmcs.h"
#include "log.h"
#include "panic.h"
#include "arch/x86/vmm/vmx_log.h"
#include "arch/x86/vmm/vmx.h"
#include "arch/x86/vmm/vmcs.h"

const char* vmx_error_to_string(uint32_t err)
{
    switch (err) {
    case 0:  return "error_not_available";
    case 1:  return "vmcall_in_vmxroot";
    case 2:  return "vmclear_invalid_phys";
    case 3:  return "vmclear_on_vmxon_pointer";
    case 4:  return "vmlaunch_on_nonclear_vmcs";
    case 5:  return "vmresume_on_nonlaunched_vmcs";
    case 6:  return "vmresume_after_vmxoff";
    case 7:  return "vmentry_invalid_control_fields";     /* 最頻出 */
    case 8:  return "vmentry_invalid_host_state";
    case 9:  return "vmptrld_invalid_phys";
    case 10: return "vmptrld_on_vmxon_pointer";
    case 11: return "vmptrld_incorrect_vmcs_revision";
    case 12: return "vmread_vmwrite_unsupported_component";
    case 13: return "vmwrite_to_readonly_field";
    case 15: return "vmxon_in_vmxroot_operation";
    case 16: return "vmentry_invalid_exec_controls";
    case 17: return "vmentry_nonlaunched_exec_controls";
    case 18: return "vmentry_with_executive_vmcs_pointer";
    case 19: return "vmcall_on_nonclear_vmcs";
    case 20: return "vmcall_invalid_vmexit_controls";
    case 22: return "vmcall_incorrect_msr_msg_revision";
    case 23: return "vmxoff_under_dual_monitor";
    case 24: return "vmcall_in_smm";
    case 25: return "vmentry_invalid_execution_ctrls";
    case 26: return "vmentry_events_blocked_by_mov_ss";
    case 28: return "invalid_invept";
    /* 必要になったら随時追加（SDM Vol.3C 31.4 参照） */
    default: return "unknown";
    }
}

/* 失敗時は 0（“error_not_available”）を返す */
uint32_t vmcs_instruction_error(void)
{
    uint64_t v = 0;
    if (vmcs_vmread(VMCS_VM_INSTRUCTION_ERROR, &v) != 0)
        return 0;
    return (uint32_t)v;
}

/* 直近の VM-instruction error を読み出してログへ出す */
void vmcs_log_error(const char* ctx)
{
    uint32_t err = vmcs_instruction_error();
    if (err == 0) {
        /* 0 は「エラー番号なし」。VMfailInvalid などの場合で、特に出力しない。 */
        KLOG_DEBUG("vmx", "%s: no instruction error (err=0)", ctx ? ctx : "(null)");
        return;
    }
    const char* msg = vmx_error_to_string(err);
    KLOG_ERROR("vmx", "%s: vm-instruction error %u (%s)", 
               ctx ? ctx : "(null)", (unsigned)err, msg);
}