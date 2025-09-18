#pragma once
#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include <stdbool.h>

typedef uint64_t Phys;
typedef uint64_t Virt;

typedef enum {
    PAGE_RO,   // R,  NX=1
    PAGE_RW,   // RW, NX=1
    PAGE_RX,   // R,  NX=0
    PAGE_RWX,  // RW, NX=0
} PageAttr;

EFI_STATUS page_set_lv4_writable(void);
EFI_STATUS page_map4k_to(uint64_t vaddr, uint64_t paddr, PageAttr attr);

// 追加: 既存マップの保護だけ変える
EFI_STATUS page_set_attr_4k(uint64_t vaddr, PageAttr attr);
EFI_STATUS page_apply_attr_range(uint64_t vaddr, uint64_t size, PageAttr attr);
