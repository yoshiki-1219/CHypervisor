#include <stdint.h>
#include <stddef.h> 
#include "arch/x86/vmm/vcpu.h"

/* C ヘルパ（シンボル名は上の C と一致させる） */
void set_host_rsp_thunk(uint64_t rsp);

/* =========================================================
 * asm_vmentry(vcpu):
 *   - callee-saved を保存
 *   - &vcpu->guest_regs を一時的にスタックに置く（VMEXIT 用）
 *   - いまの RSP を Host RSP に vmwrite（毎回）
 *   - vcpu->launch_done を見て VMRESUME/VMLAUNCH
 *   - 成功ならゲストへ飛ぶ（戻らない）、失敗なら al=1 で ret
 *   戻り値: al=0 成功（= VMEXIT 経由で戻ってくる）, al=1 失敗
 * ========================================================= */
__attribute__((naked)) uint8_t asm_vmentry(Vcpu* vcpu __attribute__((unused))) {
    __asm__ __volatile__(
        "push %%rbp\n\t"
        "push %%r15\n\t"
        "push %%r14\n\t"
        "push %%r13\n\t"
        "push %%r12\n\t"
        "push %%rbx\n\t"

        "mov  %%rdi, %%rbx\n\t"
        "add  $%c[off_guest], %%rbx\n\t"
        "push %%rbx\n\t"

        "lea  8(%%rsp), %%rdi\n\t"
        "call set_host_rsp_thunk\n\t"

        "testb $1, %c[off_launch](%%rdi)\n\t"

        "lea  %c[off_guest](%%rdi), %%rax\n\t"
        "mov  %c[off_rcx](%%rax), %%rcx\n\t"
        "mov  %c[off_rdx](%%rax), %%rdx\n\t"
        "mov  %c[off_rbx](%%rax), %%rbx\n\t"
        "mov  %c[off_rsi](%%rax), %%rsi\n\t"
        "mov  %c[off_rdi](%%rax), %%rdi\n\t"
        "mov  %c[off_rbp](%%rax), %%rbp\n\t"
        "mov  %c[off_r8] (%%rax), %%r8 \n\t"
        "mov  %c[off_r9] (%%rax), %%r9 \n\t"
        "mov  %c[off_r10](%%rax), %%r10\n\t"
        "mov  %c[off_r11](%%rax), %%r11\n\t"
        "mov  %c[off_r12](%%rax), %%r12\n\t"
        "mov  %c[off_r13](%%rax), %%r13\n\t"
        "mov  %c[off_r14](%%rax), %%r14\n\t"
        "mov  %c[off_r15](%%rax), %%r15\n\t"
        "movaps %c[off_xmm0](%%rax), %%xmm0\n\t"
        "movaps %c[off_xmm1](%%rax), %%xmm1\n\t"
        "movaps %c[off_xmm2](%%rax), %%xmm2\n\t"
        "movaps %c[off_xmm3](%%rax), %%xmm3\n\t"
        "movaps %c[off_xmm4](%%rax), %%xmm4\n\t"
        "movaps %c[off_xmm5](%%rax), %%xmm5\n\t"
        "movaps %c[off_xmm6](%%rax), %%xmm6\n\t"
        "movaps %c[off_xmm7](%%rax), %%xmm7\n\t"
        "mov  %c[off_rax](%%rax), %%rax\n\t"

        "jz 1f\n\t"
        "vmresume\n\t"
        "1:\n\t"
        "vmlaunch\n\t"

        "mov $1, %%al\n\t"
        "add $8, %%rsp\n\t"
        "pop %%rbx\n\t"
        "pop %%r12\n\t"
        "pop %%r13\n\t"
        "pop %%r14\n\t"
        "pop %%r15\n\t"
        "pop %%rbp\n\t"
        "ret\n\t"
        :
        : /* 即値オペランドは現状のままでOK */
          [off_guest]  "i"(offsetof(Vcpu, guest_regs)),
          [off_launch] "i"(offsetof(Vcpu, launch_done)),
          [off_rax]    "i"(offsetof(GuestRegisters, rax)),
          [off_rcx]    "i"(offsetof(GuestRegisters, rcx)),
          [off_rdx]    "i"(offsetof(GuestRegisters, rdx)),
          [off_rbx]    "i"(offsetof(GuestRegisters, rbx)),
          [off_rsi]    "i"(offsetof(GuestRegisters, rsi)),
          [off_rdi]    "i"(offsetof(GuestRegisters, rdi)),
          [off_rbp]    "i"(offsetof(GuestRegisters, rbp)),
          [off_r8]     "i"(offsetof(GuestRegisters, r8 )),
          [off_r9]     "i"(offsetof(GuestRegisters, r9 )),
          [off_r10]    "i"(offsetof(GuestRegisters, r10)),
          [off_r11]    "i"(offsetof(GuestRegisters, r11)),
          [off_r12]    "i"(offsetof(GuestRegisters, r12)),
          [off_r13]    "i"(offsetof(GuestRegisters, r13)),
          [off_r14]    "i"(offsetof(GuestRegisters, r14)),
          [off_r15]    "i"(offsetof(GuestRegisters, r15)),
          [off_xmm0]   "i"(offsetof(GuestRegisters, xmm0)),
          [off_xmm1]   "i"(offsetof(GuestRegisters, xmm1)),
          [off_xmm2]   "i"(offsetof(GuestRegisters, xmm2)),
          [off_xmm3]   "i"(offsetof(GuestRegisters, xmm3)),
          [off_xmm4]   "i"(offsetof(GuestRegisters, xmm4)),
          [off_xmm5]   "i"(offsetof(GuestRegisters, xmm5)),
          [off_xmm6]   "i"(offsetof(GuestRegisters, xmm6)),
          [off_xmm7]   "i"(offsetof(GuestRegisters, xmm7))
        : "cc","memory"
    );
}

/* =========================================================
 * asm_vmexit():
 *   VMEXIT エントリポイント（Host RIP）
 *   - ゲストの汎用/XMM を &guest_regs へ保存
 *   - callee-saved を復元
 *   - al=0 をセットして asm_vmentry 呼び出し元へ ret
 * ========================================================= */
__attribute__((naked)) void asm_vmexit(void) {
    __asm__ __volatile__(
        "cli\n\t"

        /* スタック先頭に &guest_regs が載っている前提（asm_vmentry が push 済） */
        "push %rax\n\t"                 /* RAX を一時退避（スクラッチに使う） */
        "mov  8(%rsp), %rax\n\t"        /* RAX = &guest_regs */

        /* 退避した RAX を保存→&guest_regs（RAX）で上書き */
        "pop  0(%rax)\n\t"              /* guest_regs.rax = saved RAX */
        "add  $8, %rsp\n\t"             /* &guest_regs を破棄 */

        /* 残りのレジスタを保存 */
        "mov  %rcx,   8(%rax)\n\t"
        "mov  %rdx,  16(%rax)\n\t"
        "mov  %rbx,  24(%rax)\n\t"
        "mov  %rbp,  32(%rax)\n\t"
        "mov  %rsi,  40(%rax)\n\t"
        "mov  %rdi,  48(%rax)\n\t"
        "mov  %r8,   56(%rax)\n\t"
        "mov  %r9,   64(%rax)\n\t"
        "mov  %r10,  72(%rax)\n\t"
        "mov  %r11,  80(%rax)\n\t"
        "mov  %r12,  88(%rax)\n\t"
        "mov  %r13,  96(%rax)\n\t"
        "mov  %r14, 104(%rax)\n\t"
        "mov  %r15, 112(%rax)\n\t"
        "movaps %xmm0, 128(%rax)\n\t"
        "movaps %xmm1, 144(%rax)\n\t"
        "movaps %xmm2, 160(%rax)\n\t"
        "movaps %xmm3, 176(%rax)\n\t"
        "movaps %xmm4, 192(%rax)\n\t"
        "movaps %xmm5, 208(%rax)\n\t"
        "movaps %xmm6, 224(%rax)\n\t"
        "movaps %xmm7, 240(%rax)\n\t"

        /* callee-saved を復元（asm_vmentry の逆順） */
        "pop %rbx\n\t"
        "pop %r12\n\t"
        "pop %r13\n\t"
        "pop %r14\n\t"
        "pop %r15\n\t"
        "pop %rbp\n\t"

        /* 成功で C に返る合図: al=0 */
        "xor %eax, %eax\n\t"
        "ret\n\t"
    );
}
