// bootloader/boot.c
#include <efi.h>
#include <efilib.h>
#include "log.h"
#include "file.h"
#include "arch_x86_page.h"
#include "kernel_loader.h"
#include "memmap.h"
#include "bootinfo.h" 

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);
    log_init(SystemTable);
    log_set_scope(L"boot");
    log_set_level(LOG_DEBUG);

    if (!ST || !BS) return EFI_ABORTED;

    EFI_STATUS st = init_file();
    if (EFI_ERROR(st)) return st;
    OpenRoot();

    UINT64 entry = 0;
    st = load_kernel_elf(L"kernel.elf", &entry);
    if (EFI_ERROR(st)) {
        log_printf(LOG_ERROR, L"load_kernel_elf failed: %r", st);
        CloseRoot();
        for (;;) ST->BootServices->Stall(1000000);
        return st;
    }

    CloseRoot();

    // --- メモリマップを取得しておく（ExitBootServicesに必須） ---
    MEMORY_MAP mm;
    st = memmap_init(&mm, 4); // 4ページ(≈16KiB)分
    if (EFI_ERROR(st)) { log_printf(LOG_ERROR, L"memmap_init failed: %r", st); goto halt; }

    st = memmap_refresh(&mm);
    if (EFI_ERROR(st)) { log_printf(LOG_ERROR, L"GetMemoryMap failed: %r", st); goto halt; }

    memmap_print(&mm); // ここまではログ可

    // --- カーネルに渡す BootInfo を用意 ---
    BOOT_INFO bi = {
        .magic       = BOOTINFO_MAGIC,
        .memory_map  = mm,           // descriptors は LoaderData で確保済み。Exit後も内容は残る
    };

    // --- ExitBootServices（以降はログ禁止） ---
    st = exit_boot_services_with_map(ImageHandle, &mm);
    if (EFI_ERROR(st)) goto halt_silent;

    // --- カーネルのエントリにジャンプ ---
    // UEFI/MS x64 calling convention で呼ぶ（EFIAPI = ms_abi）
    typedef __attribute__((noreturn)) VOID (EFIAPI *kernel_entry_t)(const BOOT_INFO *bi);
    kernel_entry_t kentry = (kernel_entry_t)(uintptr_t)entry;

    kentry(&bi);

halt_silent:
    for(;;) { __asm__ __volatile__("hlt"); }

halt:
    for(;;) { ST->BootServices->Stall(1000000); }
    return EFI_SUCCESS;
}
