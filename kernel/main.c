#include <stdint.h>
#include "serial.h"
#include "bootinfo.h"
#include "log.h"
#include "vm.h"
#include "common.h"
#include "arch/x86/gdt.h"
#include "arch/x86/idt.h"
#include "arch/x86/paging.h"
#include "bin_alloc.h"
#include "arch/x86/pic.h"
#include "page_alloc.h"
#include "memmap.h"
#include "linux_guest.h"
#include "panic.h"
#include "arch/x86/vmm/vmx.h"
#include "arch/x86/vmm/vmcs.h"
#include "arch/x86/vmm/vmx_log.h"
#include "arch/x86/vmm/vcpu.h"
#include "arch/x86/vmm/ept.h"
#include "arch/x86/interrupt.h"

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
        "cli                      \n\t"
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

    // /* --- ログ初期化 --- */
    klog_init(&com1, (klog_options_t){ .level = KLOG_DEBUG });

    if (bootinfo_snapshot_init(bi) != 0) {
        KLOG_ERROR("main", "bootinfo snapshot failed");
        for(;;) __asm__ __volatile__("hlt");
    }
    
    KLOG_INFO("main", "Booting kernel...");
    
    gdt_init();
    intr_init_all_vectors();
    KLOG_INFO("main", "Initialized GDT.");

    idt_init();
    intr_init_all_vectors();
    KLOG_INFO("main", "Initialized IDT.");

    const MEMORY_MAP *map = bootinfo_snapshot_memmap();
    page_allocator_init(map);

    KLOG_INFO("main", "Reconstructing memory mapping...");
    if (paging_reconstruct_and_mark() != 0) {
        KLOG_ERROR("main", "paging reconstruct failed");
        for(;;) __asm__ __volatile__("hlt");
    }
    KLOG_INFO("main", "Paging is reconstructed.");
    
    page_allocator_release_boot_services_data(map);
    KLOG_INFO("main", "BootServicesData released to allocator.");

    bin_alloc_init();
    KLOG_INFO("main", "Initialized bin allocator.");

    pic_init();
    KLOG_INFO("main", "Initialized PIC.");

    Vm   *vm   = kmalloc(sizeof(Vm), PAGE_SIZE_4K);
    Vcpu *vcpu = &vm->vcpu;
    vcpu->vpid = 1;
    vcpu->id   = 1;

    KLOG_INFO("main", "Initialized VMX");
    if (vmx_init_and_enter(vcpu) != 0) {
        KLOG_ERROR("kmain", "VMX root entry failed");
        panic("VMXON failed");
    }

    KLOG_INFO("main", "Allocating VMCS...");
    if (vmcs_alloc_and_load(vcpu) != 0) {
        KLOG_ERROR("main", "Allocating VMCS failed");
        panic("VMCS load failed");
    }

    KLOG_INFO("main", "Setting VMCS...");
    if (vcpu_build_vmcs(vcpu) != 0) {
        KLOG_ERROR("main", "Setting VMCS failed");
         panic("VMCS set failed");
    }

    KLOG_INFO("main", "Setting EPT...");
    if (vcpu_setup_ept(vcpu) != 0){
        KLOG_ERROR("main", "Setting EPT falied");
         panic("EPT set failed");
    }

    vm->guest_mem_size = 100 * 1024 * 1024;
    vm->guest_mem = (void*)(uintptr_t)vm->vcpu.guest_base;
    GUEST_INFO gi;
    gi.guest_image = phys2virt((uint64_t)bootinfo_snapshot_guestinfo()->guest_image);
    gi.guest_size  = bootinfo_snapshot_guestinfo()->guest_size;
    loadKernel(vm, &gi);

    memcpy(vm->guest_mem + 0x00000, blobGuest, 0x20);
    memcpy(vm->guest_mem + 0x100000, blobGuest, 0x20);

    KLOG_INFO("main", "Starting virtual machine...");
    vcpu_loop(vcpu);

    for (;;) __asm__ __volatile__("hlt");
}