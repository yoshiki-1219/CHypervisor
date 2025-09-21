#pragma once
#include <stdint.h>
#include "arch/x86/vmm/ept.h"
#include "arch/x86/vmm/vmcs.h"
#include "arch/x86/vmm/vmx.h"
#include "arch/x86/vmm/vcpu.h"
#include "arch/x86/vmm/vmx_vpid.h"
#include "arch/x86/msr.h"

/*  INVVPID の全オプションが使える時のみ “サポートあり” と判定 */
int vmx_is_vpid_supported(void)
{
    vmx_ept_vpid_cap_t cap;
    cap.u64 = rdmsr(IA32_VMX_EPT_VPID_CAP);

    return (cap.bits.invvpid &&
            cap.bits.invvpid_single &&
            cap.bits.invvpid_all &&
            cap.bits.invvpid_individual &&
            cap.bits.invvpid_single_globals);
}

/* VMCS の vpid フィールドに値を書き込むヘルパ */
int vmx_write_vpid(uint16_t vpid)
{
    return vmcs_vmwrite(VMCS_VPID, (uint64_t)vpid);
}
