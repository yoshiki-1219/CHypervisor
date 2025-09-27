#pragma once
#include <stdint.h>
#include <stddef.h>
#include "arch/x86/vmm/vcpu.h"
#include "bootinfo.h"

typedef __attribute__((aligned(16))) struct{
    Vcpu     vcpu;
    uint64_t guest_mem_size;
    void*    guest_mem;
} Vm;

int loadKernel(Vm *vm, GUEST_INFO* guest_info);