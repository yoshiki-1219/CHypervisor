#include <stdint.h>
#include "serial.h"
#include "bootinfo.h"
#include "log.h"
#include "arch/x86/gdt.h"
#include "arch/x86/idt.h"
#include "arch/x86/isr.h"
#include "arch/x86/paging.h"
#include "bin_alloc.h"
#include "arch/x86/pic.h"
#include "page_alloc.h"
#include "memmap.h"
#include "panic.h"

/* リンカスクリプトで定義するスタック境界シンボル
   - 配列ではなく「オブジェクトの先頭アドレス」という意味で uint8_t を使う
   - 「下側（ガード側）から 16 バイト引いた所」を新しい RSP にする */
extern uint8_t __stackguard_lower[];
extern uint8_t __stack_top[];

__attribute__((sysv_abi))
static void kernelTrampoline(void *boot_info);
static void kernelMain(BOOT_INFO *bi);

void kernelEntry(void *bi)   // SysV: 第1引数は RDI
{
    /* RSP は「関数入口で 16B 整列」になるように。call 直前は RSP≡8(mod16) */
    uintptr_t new_sp = ((uintptr_t)__stack_top & ~(uintptr_t)0xF) - 8;

    __asm__ __volatile__ (
        ".intel_syntax noprefix   \n\t"
        "mov   rsp, %0            \n\t"  // 新スタック
        "mov   rdi, %2            \n\t"  // 第1引数 = bi（明示して安心）
        "call  %1                 \n\t"  // ※ Intel 構文なので '*' は付けない
        "1: hlt                   \n\t"
        "jmp   1b                 \n\t"
        ".att_syntax prefix       \n\t"
        :
        : "r"(new_sp),            // %0
          "r"(kernelTrampoline),  // %1
          "r"(bi)                 // %2
        : "memory", "rdi"
    );

    __builtin_unreachable();
}

__attribute__((sysv_abi))
static void kernelTrampoline(void *boot_info)
{
    BOOT_INFO *bi = (BOOT_INFO*)boot_info;

    if (bi->magic != BOOTINFO_MAGIC) {
        // ここでログが必要なら一時的にシリアル直接出力でもよい
        for (;;) __asm__ __volatile__("hlt");
    }

    kernelMain(bi);
    for (;;) __asm__ __volatile__("hlt");
}

static void kernelMain(BOOT_INFO *bi)
{
    /* --- シリアル初期化 --- */
    serial_device_t com1;
    serial_init(&com1, COM1, 115200);

    /* --- ログ初期化 --- */
    klog_init(&com1, (klog_options_t){ .level = KLOG_DEBUG });

    if (bootinfo_snapshot_init(bi) != 0) {
        KLOG_ERROR("main", "bootinfo snapshot failed");
        for(;;) __asm__ __volatile__("hlt");
    }
    
    KLOG_INFO("main", "Booting kernel...");
    KLOG_DEBUG("main", "BOOTINFO magic=0x%016llX",
               (unsigned long long)bi->magic);

    /* GDT → IDT（順序はこのままでOK） */
    gdt_init();
    KLOG_INFO("main", "Initialized GDT.");

    intr_init_all_vectors();   /* IDT 構築＋LIDT＋STI */
    KLOG_INFO("main", "Initialized IDT.");

    page_allocator_init(bootinfo_snapshot_memmap());
    KLOG_INFO("main", "Reconstructing memory mapping...");
    if (paging_reconstruct_and_mark() != 0) {
        KLOG_ERROR("main", "paging reconstruct failed");
        for(;;) __asm__ __volatile__("hlt");
    }
    KLOG_INFO("main", "Paging is reconstructed.");
    page_allocator_release_boot_services_data(bootinfo_snapshot_memmap());
    KLOG_INFO("main", "BootServicesData released to allocator.");

    bin_alloc_init();
    KLOG_INFO("main", "Initialized bin allocator.");

    pic_init();
    KLOG_INFO("main", "Initialized PIC.");

    for (;;) __asm__ __volatile__("hlt");
}
