// bootloader/bootinfo.h
#pragma once
#include <efi.h>
#include <efilib.h>
#include "memmap.h"  // MEMORY_MAP を使う

#define BOOTINFO_MAGIC 0xDEADBEEFCAFEBABEull

typedef struct {
    UINT64    magic;      // 検証用
    MEMORY_MAP memory_map; // ExitBootServices前に取得したメモリマップ
} BOOT_INFO;
