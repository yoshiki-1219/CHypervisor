// kernel/arch/x86/vmm/vcpu.h
#pragma once
#include <stdint.h>

/* ゲスト一般レジスタ + XMM0-7（16Bアライン） */
typedef struct __attribute__((packed, aligned(16))) GuestRegisters {
    uint64_t rax, rcx, rdx, rbx, rbp, rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint8_t  xmm0[16], xmm1[16], xmm2[16], xmm3[16];
    uint8_t  xmm4[16], xmm5[16], xmm6[16], xmm7[16];
} GuestRegisters;

/* vCPU 状態（最小） */
typedef struct Vcpu {
    GuestRegisters guest_regs;
    int            launch_done; /* 0:まだ, 1:VMLAUNCH 済(以後 VMRESUME) */
} Vcpu;

/* API */
int  vcpu_loop(Vcpu* vcpu);             /* VM Entry/Exit ループ（戻らない想定） */
void vcpu_setup_vmexit_rip(void);       /* Host RIP に asm_vmexit を設定 */

/* VMExit から C 側に戻すハンドラ（必要なら自由に拡張） */
void vmexit_dispatch(Vcpu* vcpu);

/* VMX Root に入った後、VMCS を構築して VMLAUNCH する */
int vcpu_build_vmcs_and_launch(void);

/* HLT ループするだけの最小ゲスト */
void vcpu_guest_hlt_loop(void) __attribute__((noreturn));
