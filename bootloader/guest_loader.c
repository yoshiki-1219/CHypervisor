// bootloader/guest_loader.c
#include <efi.h>
#include <efilib.h>
#include "log.h"
#include "file.h"

#define PAGE_SIZE_4K 4096ull
static inline UINTN pages_for_size(UINTN bytes) {
    return (bytes + (PAGE_SIZE_4K - 1)) / PAGE_SIZE_4K;
}

EFI_STATUS load_guest_bzimage(CONST CHAR16 *filename,
                              EFI_PHYSICAL_ADDRESS *out_start,
                              UINTN *out_size)
{
    if (!Root || !filename || !filename[0] || !out_start || !out_size) {
        return EFI_INVALID_PARAMETER;
    }

    EFI_STATUS st;
    EFI_FILE_HANDLE file = NULL;

    // 1) ファイルを開く
    st = uefi_call_wrapper(Root->Open, 5, Root, &file,
                           (CHAR16*)filename, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st) || !file) {
        log_printf(LOG_ERROR, L"Open(%s) failed: %r", filename, st);
        return st ? st : EFI_NOT_FOUND;
    }

    // 2) ファイルサイズ取得（GetInfo）
    UINTN info_size = 0;
    st = uefi_call_wrapper(file->GetInfo, 4, file, &gEfiFileInfoGuid, &info_size, NULL);
    if (st != EFI_BUFFER_TOO_SMALL) { goto fail_close; }

    EFI_FILE_INFO *info = NULL;
    st = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, info_size, (VOID**)&info);
    if (EFI_ERROR(st) || !info) { st = st ? st : EFI_OUT_OF_RESOURCES; goto fail_close; }

    st = uefi_call_wrapper(file->GetInfo, 4, file, &gEfiFileInfoGuid, &info_size, info);
    if (EFI_ERROR(st)) { log_printf(LOG_ERROR, L"GetInfo failed: %r", st); goto fail_free_info; }

    UINTN file_size = (UINTN)info->FileSize;
    if (file_size == 0) { st = EFI_LOAD_ERROR; goto fail_free_info; }

    // 3) EfiLoaderData でページ確保（Anywhere）
    UINTN pages = pages_for_size(file_size);
    EFI_PHYSICAL_ADDRESS phys = 0;
    st = uefi_call_wrapper(BS->AllocatePages, 4,
                           AllocateAnyPages, EfiLoaderData, pages, &phys);
    if (EFI_ERROR(st)) {
        log_printf(LOG_ERROR, L"AllocatePages(LoaderData) failed: %r", st);
        goto fail_free_info;
    }

    // 4) 読み込み
    UINTN to_read = file_size;
    st = uefi_call_wrapper(file->Read, 3, file, &to_read, (VOID*)(UINTN)phys);
    if (EFI_ERROR(st) || to_read != file_size) {
        log_printf(LOG_ERROR, L"Read(bzImage) failed: %r (read=%lu/%lu)", st, (UINT64)to_read, (UINT64)file_size);
        // 必要なら FreePages で解放してもよいが、LoaderData のままでも ExitBootServices 後に破棄されない。
        goto fail_free_info;
    }

    log_printf(LOG_INFO, L"Loaded bzImage @ 0x%lx ~ 0x%lx (%lu bytes)",
               (UINT64)phys, (UINT64)(phys + file_size), (UINT64)file_size);

    *out_start = phys;
    *out_size  = file_size;
    st = EFI_SUCCESS;

fail_free_info:
    if (info) uefi_call_wrapper(BS->FreePool, 1, info);
fail_close:
    if (file) uefi_call_wrapper(file->Close, 1, file);
    return st;
}
