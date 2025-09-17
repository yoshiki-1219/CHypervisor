// bootloader/memmap.c
#include <efi.h>
#include <efilib.h>
#include "memmap.h"
#include "log.h"

#ifndef EFI_PAGE_SIZE
#define EFI_PAGE_SIZE 4096
#endif

static CONST CHAR16* mem_type_str(EFI_MEMORY_TYPE t) {
    switch (t) {
        case EfiReservedMemoryType:      return L"EfiReservedMemoryType";
        case EfiLoaderCode:              return L"EfiLoaderCode";
        case EfiLoaderData:              return L"EfiLoaderData";
        case EfiBootServicesCode:        return L"EfiBootServicesCode";
        case EfiBootServicesData:        return L"EfiBootServicesData";
        case EfiRuntimeServicesCode:     return L"EfiRuntimeServicesCode";
        case EfiRuntimeServicesData:     return L"EfiRuntimeServicesData";
        case EfiConventionalMemory:      return L"EfiConventionalMemory";
        case EfiUnusableMemory:          return L"EfiUnusableMemory";
        case EfiACPIReclaimMemory:       return L"EfiACPIReclaimMemory";
        case EfiACPIMemoryNVS:           return L"EfiACPIMemoryNVS";
        case EfiMemoryMappedIO:          return L"EfiMemoryMappedIO";
        case EfiMemoryMappedIOPortSpace: return L"EfiMemoryMappedIOPortSpace";
        case EfiPalCode:                 return L"EfiPalCode";
        #ifdef EfiPersistentMemory
        case EfiPersistentMemory:        return L"EfiPersistentMemory";
        #endif
        #ifdef EfiUnacceptedMemoryType
        case EfiUnacceptedMemoryType:    return L"EfiUnacceptedMemoryType";
        #endif
        default:                         return L"Unknown";
    }
}

EFI_STATUS memmap_init(MEMORY_MAP *mm, UINTN pages)
{
    if (!mm || pages == 0) return EFI_INVALID_PARAMETER;

    mm->buffer_size = pages * EFI_PAGE_SIZE;
    mm->map_size = mm->buffer_size;
    mm->map_key = 0;
    mm->descriptor_size = 0;
    mm->descriptor_version = 0;
    mm->descriptors = NULL;

    EFI_STATUS st = uefi_call_wrapper(
        BS->AllocatePool, 3,
        EfiLoaderData,
        mm->buffer_size,
        (VOID**)&mm->descriptors
    );
    if (EFI_ERROR(st)) return st;

    return EFI_SUCCESS;
}

EFI_STATUS memmap_refresh(MEMORY_MAP *mm)
{
    if (!mm || !mm->descriptors) return EFI_INVALID_PARAMETER;

    // 1回目：今のバッファで試す
    UINTN size = mm->buffer_size;
    EFI_STATUS st = uefi_call_wrapper(
        BS->GetMemoryMap, 5,
        &size,
        mm->descriptors,
        &mm->map_key,
        &mm->descriptor_size,
        &mm->descriptor_version
    );

    if (st == EFI_BUFFER_TOO_SMALL) {
        // 必要サイズが返るので、少し余裕を見て取り直す
        size += 2 * mm->descriptor_size + EFI_PAGE_SIZE;

        // 古いバッファを破棄して取り直し
        if (mm->descriptors) {
            uefi_call_wrapper(BS->FreePool, 1, mm->descriptors);
            mm->descriptors = NULL;
        }

        mm->buffer_size = size;
        mm->map_size = size;

        st = uefi_call_wrapper(
            BS->AllocatePool, 3,
            EfiLoaderData,
            mm->buffer_size,
            (VOID**)&mm->descriptors
        );
        if (EFI_ERROR(st)) return st;

        // 取り直したバッファで再取得
        st = uefi_call_wrapper(
            BS->GetMemoryMap, 5,
            &mm->map_size,
            mm->descriptors,
            &mm->map_key,
            &mm->descriptor_size,
            &mm->descriptor_version
        );
    } else {
        // 成功時：実サイズを反映
        mm->map_size = size;
    }

    return st;
}

VOID memmap_print(const MEMORY_MAP *mm)
{
    if (!mm || !mm->descriptors || mm->map_size == 0 || mm->descriptor_size == 0) return;

    log_printf(LOG_INFO, L"UEFI Memory Map (size=%lu, desc=%lu bytes, ver=%u, key=%lu)",
               (UINT64)mm->map_size, (UINT64)mm->descriptor_size,
               (UINT32)mm->descriptor_version, (UINT64)mm->map_key);

    UINT8 *p   = (UINT8*)mm->descriptors;
    UINT8 *end = p + mm->map_size;

    while (p < end) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR*)p;
        UINT64 start = d->PhysicalStart;
        UINT64 bytes = (UINT64)d->NumberOfPages * EFI_PAGE_SIZE;
        log_printf(LOG_DEBUG, L"  0x%016lx - 0x%016lx : %s",
                   (UINT64)start, (UINT64)(start + bytes), mem_type_str(d->Type));
        p += mm->descriptor_size;
    }
}

EFI_STATUS exit_boot_services_with_map(EFI_HANDLE ImageHandle, MEMORY_MAP *mm)
{
    if (!mm) return EFI_INVALID_PARAMETER;

    // まず最新のマップを取得
    EFI_STATUS st = memmap_refresh(mm);
    if (EFI_ERROR(st)) {
        log_printf(LOG_ERROR, L"GetMemoryMap failed before ExitBootServices: %r", st);
        return st;
    }

    // 1回目の ExitBootServices
    st = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, mm->map_key);
    if (EFI_ERROR(st)) {
        // 失敗したら、再度メモリマップを取り直して再試行
        // （GetMemoryMap の間にメモリマップが更新された可能性があるため）
        EFI_STATUS st2 = memmap_refresh(mm);
        if (EFI_ERROR(st2)) {
            log_printf(LOG_ERROR, L"GetMemoryMap (retry) failed: %r", st2);
            return st2;
        }

        st = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, mm->map_key);
        if (EFI_ERROR(st)) {
            log_printf(LOG_ERROR, L"ExitBootServices failed: %r", st);
            return st;
        }
    }

    // ここから先は Boot Services 使用不可（ログも NG）
    return EFI_SUCCESS;
}

VOID memmap_destroy(MEMORY_MAP *mm)
{
    if (!mm) return;
    if (mm->descriptors) {
        uefi_call_wrapper(BS->FreePool, 1, mm->descriptors);
        mm->descriptors = NULL;
    }
    mm->buffer_size = mm->map_size = mm->map_key = mm->descriptor_size = 0;
    mm->descriptor_version = 0;
}
