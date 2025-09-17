// kernel/main.c
#include <stdint.h>
#include "serial.h"      /* 追加 */

/* リンカスクリプトで定義するシンボル */
extern char __stackguard_lower[];
extern char __stack_top[];

typedef struct {
    uint64_t magic;
} BootInfo;

/* SysV ABI の C 関数へ渡すトランポリン */
__attribute__((sysv_abi))
static void kernelTrampoline(void *boot_info);

__attribute__((naked, ms_abi))
void kernelEntry(void *boot_info_msabi __attribute__((unused)))
{
    __asm__ __volatile__ (
        ".intel_syntax noprefix   \n\t"
        "mov rdi, rcx             \n\t"  // RCX → RDI
        "mov rsp, %[new_rsp]      \n\t"
        "call %[tramp]            \n\t"
        "1: hlt                   \n\t"
        "jmp 1b                   \n\t"
        ".att_syntax prefix       \n\t"
        :
        : [tramp]"r"(kernelTrampoline),
          [new_rsp]"r"((uintptr_t)__stackguard_lower - 0x10)
        : "memory"
    );
}

static void kernelMain(BootInfo *bi)
{
    (void)bi;

    /* --- シリアルの初期化とテスト出力 --- */
    serial_device_t com1;
    serial_init(&com1, COM1, 115200);
    serial_writeln(&com1, "Hello, Kernel via COM1!");

    for (;;) __asm__ __volatile__("hlt");
}

__attribute__((sysv_abi))
static void kernelTrampoline(void *boot_info)
{
    kernelMain((BootInfo*)boot_info);
    for (;;) __asm__ __volatile__("hlt");
}
