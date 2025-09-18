#pragma once
#include <stdint.h>
#include "arch/x86/paging.h"

#define PAGE_SIZE_4K            0x1000ULL
#define PAGE_SHIFT_4K           12

/* 仮想アドレスレイアウト */
#define DIRECT_MAP_BASE         0xFFFF888000000000ULL
#define DIRECT_MAP_SIZE         (512ULL << 30)       /* 512 GiB = 1 << 39 */
#define KERNEL_BASE             0xFFFFFFFF80000000ULL
#define KERNEL_TEXT_BASE        0xFFFFFFFF80100000ULL

/* ページング（レベルごとのシフト） */
#define LV1_SHIFT               12
#define LV2_SHIFT               21
#define LV3_SHIFT               30
#define LV4_SHIFT               39
#define PT_ENTRIES              512
#define PT_INDEX_MASK           0x1FFULL

