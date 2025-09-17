#include <efi.h>
#include <efilib.h>
#include "file.h"
#include "log.h"

struct FILE file_list[MAX_FILE_NUM];
EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SFSP = NULL;
EFI_FILE_HANDLE Root = NULL;

static VOID strncpy16(CHAR16 *dst, CONST CHAR16 *src, UINTN maxlen) {
    if (!dst || !src || maxlen == 0) return;
    UINTN i = 0;
    for (; i + 1 < maxlen && src[i] != L'\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = L'\0';
}


EFI_STATUS init_file(void)
{
    EFI_STATUS Status;

    Status = LibLocateProtocol(
        (EFI_GUID*)&gEfiSimpleFileSystemProtocolGuid,
        (VOID**)&SFSP
    );

    if (EFI_ERROR(Status) || !SFSP) {
        log_printf(LOG_ERROR, L"Located simple file system protocol failed: %r", Status);
        SFSP = NULL;
        return Status;
    }

    log_printf(LOG_INFO, L"Located simple file system protocol.");
    return EFI_SUCCESS;
}


int OpenRoot(void)
{
    if (!SFSP) {
        log_printf(LOG_ERROR, L"SFSP is NULL. Did you call init_file()?");
        return 0;
    }

    EFI_STATUS Status;


    Status = uefi_call_wrapper(SFSP->OpenVolume, 2, SFSP, &Root);
    if (EFI_ERROR(Status) || !Root) {
        log_printf(LOG_ERROR, L"SFSP->OpenVolume failed: %r", Status);
        return 0;
    }
    log_printf(LOG_INFO, L"Opened filesystem volume.");

    UINTN idx = 0;
    UINT8 buf[MAX_FILE_BUF];

    for (;;) {
        if (idx >= MAX_FILE_NUM) break;

        UINTN buf_size = sizeof(buf);
        Status = uefi_call_wrapper(Root->Read, 3, Root, &buf_size, buf);
        if (EFI_ERROR(Status)) {
            log_printf(LOG_ERROR, L"Root->Read failed: %r", Status);
            break;
        }
        if (buf_size == 0) {
            break;  // 読み切り
        }

        EFI_FILE_INFO *info = (EFI_FILE_INFO *)buf;

        strncpy16(file_list[idx].name,
                  info->FileName ? info->FileName : L"",
                  MAX_FILE_NAME_LEN);

        if (info->Attribute & EFI_FILE_DIRECTORY) {
            log_printf(LOG_INFO, L"[DIR ] %s", file_list[idx].name);
        } else {
            log_printf(LOG_INFO, L"[FILE] %s (%lu bytes)",
                       file_list[idx].name, info->FileSize);
        }

        ++idx;
    }

    log_printf(LOG_INFO, L"Listed %d entries in root.", (int)idx);
    return (int)idx;
}

int CloseRoot(void)
{
    uefi_call_wrapper(Root->Close, 1, Root);
    Root = NULL;
    log_printf(LOG_INFO, L"Closed filesystem volume.");
    return (int)0;
}

EFI_STATUS file_read(CONST CHAR16 *filename, VOID *buf, UINTN *size)
{
    if (!Root) {
        log_printf(LOG_ERROR, L"Root is NULL. Did you call OpenRoot()?");
        return EFI_NOT_READY;
    }
    if (!filename || !filename[0] || !buf || !size) {
        return EFI_INVALID_PARAMETER;
    }

    EFI_STATUS Status;
    EFI_FILE_HANDLE File = NULL;

    // 1) ルートからファイルを READ で開く
    Status = uefi_call_wrapper(Root->Open, 5,
                               Root, &File,
                               (CHAR16*)filename,
                               EFI_FILE_MODE_READ,
                               0);
    if (EFI_ERROR(Status) || !File) {
        log_printf(LOG_ERROR, L"Open(%s) failed: %r", filename, Status);
        return Status ? Status : EFI_NOT_FOUND;
    }

    // 2) 読み込み（最大 *size バイト）
    UINTN total = 0;
    UINT8 *p = (UINT8*)buf;
    UINTN cap = *size;

    while (total < cap) {
        UINTN chunk = cap - total;  // 残り容量ぶん要求
        Status = uefi_call_wrapper(File->Read, 3, File, &chunk, p + total);
        if (EFI_ERROR(Status)) {
            log_printf(LOG_ERROR, L"Read(%s) failed: %r", filename, Status);
            break;
        }
        if (chunk == 0) {
            // EOF
            Status = EFI_SUCCESS;
            break;
        }
        total += chunk;
    }

    uefi_call_wrapper(File->Close, 1, File);

    *size = total;
    return Status;
}

