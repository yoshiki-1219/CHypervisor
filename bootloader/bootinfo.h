// bootloader/bootinfo.h
#pragma once
#include <efi.h>
#include <efilib.h>
#include "memmap.h"  // MEMORY_MAP を使う

#define BOOTINFO_MAGIC 0xDEADBEEFCAFEBABEull

typedef struct {
    VOID   *guest_image;  // bzImage をロードした物理アドレス（後述注意）
    UINTN   guest_size;   // bzImage のバイトサイズ
} GUEST_INFO;

typedef struct {
    UINT64     magic;       // 検証用
    MEMORY_MAP memory_map;  // ExitBootServices前に取得したメモリマップ
    GUEST_INFO guest_info;  // 追加: ゲスト(bzImage)情報
} BOOT_INFO;
