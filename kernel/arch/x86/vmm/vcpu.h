// kernel/arch/x86/vmm/vcpu.h
#pragma once
#include <stdint.h>
#include "arch/x86/vmm/ept.h"

struct Eptp;

typedef struct __attribute__((aligned(16))) GuestRegisters {
    uint64_t rax, rcx, rdx, rbx, rbp, rsi, rdi;
    uint64_t r8,  r9,  r10, r11, r12, r13, r14, r15;
    // ここで +8B のパディングを入れて 128B(0x80) に揃える
    uint64_t _pad;  // 必須
    uint8_t  xmm0[16], xmm1[16], xmm2[16], xmm3[16];
    uint8_t  xmm4[16], xmm5[16], xmm6[16], xmm7[16];
} GuestRegisters;

/* vCPU 状態（最小） */
typedef struct Vcpu {
    GuestRegisters guest_regs;
    int            launch_done; /* 0:まだ, 1:VMLAUNCH 済(以後 VMRESUME) */
    uint32_t       id;
    uint16_t       vpid;
    void*          vmxon_region;
    void*          vmcs_region;
    Eptp         eptp;
    uint64_t       guest_base;

} Vcpu;

/* API */
int  vcpu_loop(Vcpu* vcpu);             /* VM Entry/Exit ループ（戻らない想定） */
void set_host_rsp_thunk(uint64_t rsp);
void blobGuest(void);

/* VMExit から C 側に戻すハンドラ（必要なら自由に拡張） */
void vmexit_dispatch(Vcpu* vcpu);

/* VMX Root に入った後、VMCS を構築して VMLAUNCH する */
int vcpu_build_vmcs(Vcpu* vcpu);
