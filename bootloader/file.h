#pragma once
#include <efi.h>
#include <efilib.h>

#define MAX_FILE_NAME_LEN   16
#define MAX_FILE_NUM        16
#define MAX_FILE_BUF        1024

struct FILE {
    CHAR16 name[MAX_FILE_NAME_LEN];  // ファイル名（UTF-16）
};

// file_list 配列（実体は file.c に定義）
extern struct FILE file_list[MAX_FILE_NUM];

// Simple File System Protocol ポインタ（実体は file.c に定義）
extern EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SFSP;
extern EFI_FILE_HANDLE Root;

// 関数プロトタイプ
EFI_STATUS init_file(void);
int OpenRoot(void);
int CloseRoot(void);
EFI_STATUS file_read(CONST CHAR16 *filename, VOID *buf, UINTN *inout_size);
