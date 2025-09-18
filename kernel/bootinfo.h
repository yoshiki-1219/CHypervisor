// kernel/bootinfo.h
#pragma once
#include <stdint.h>

#if INTPTR_MAX == INT64_MAX
typedef uint64_t UINTN;
#else
#error "This kernel assumes x86_64."
#endif

typedef struct {
    UINTN   buffer_size;
    void   *descriptors;
    UINTN   map_size;
    UINTN   map_key;
    UINTN   descriptor_size;
    uint32_t descriptor_version;
} MEMORY_MAP;

/* ====== UEFI メモリディスクリプタ（EFI1.10 準拠相当） ======
 * 参考：Type/PhysicalStart/VirtualStart/NumberOfPages/Attribute
 */
typedef struct {
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} __attribute__((packed)) EFI_MEMORY_DESCRIPTOR;

/* 使う Type 値（EDK2 定義と同じ） */
enum {
    EfiReservedMemoryType      = 0,
    EfiLoaderCode              = 1,
    EfiLoaderData              = 2,
    EfiBootServicesCode        = 3,
    EfiBootServicesData        = 4,
    EfiRuntimeServicesCode     = 5,
    EfiRuntimeServicesData     = 6,
    EfiConventionalMemory      = 7,
    EfiUnusableMemory          = 8,
    EfiACPIReclaimMemory       = 9,
    EfiACPIMemoryNVS           = 10,
    EfiMemoryMappedIO          = 11,
    EfiMemoryMappedIOPortSpace = 12,
    EfiPalCode                 = 13,
    /* …以降は今回は未使用 … */
};

#define BOOTINFO_MAGIC 0xDEADBEEFCAFEBABEull

typedef struct {
    uint64_t  magic;
    MEMORY_MAP memory_map;
} BOOT_INFO;


/* 追加：スナップショット API */
int                 bootinfo_snapshot_init(const BOOT_INFO* src_bi);
/* 「同じレイアウト」の BOOT_INFO を返す（descriptors はカーネル内バッファを指す） */
const BOOT_INFO*    bootinfo_snapshot(void);
/* メモリマップだけ欲しいとき */
const MEMORY_MAP*   bootinfo_snapshot_memmap(void);