#include <stdint.h>
#include <string.h>
#include "vmcs.h"
#include "log.h"
#include "panic.h"
#include "arch/x86/vmm/vmx_log.h"
#include "arch/x86/vmm/vmx.h"
#include "arch/x86/vmm/vmcs.h"
#include "arch/x86/vmm/vcpu.h"

uint8_t asm_vmentry(Vcpu* vcpu);
void asm_vmexit(void);