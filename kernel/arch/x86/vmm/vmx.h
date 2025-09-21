#pragma once
#include <stdint.h>
#include <stddef.h>
#include "arch/x86/vmm/vcpu.h"

/* 公開 API：VMX Root Operation へ入る */
int vmx_init_and_enter(Vcpu* vcpu);
/* 終了（必要になったら実装、今は未使用） */
int vmx_leave(void);
