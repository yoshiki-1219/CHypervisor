#pragma once
#include <stddef.h>
#include <stdint.h>
#include "bootinfo.h"   /* MEMORY_MAP を使います */

/* 4KiB 固定 */
#define PAGE_SIZE   4096ULL
#define PAGE_MASK   (PAGE_SIZE - 1)

/* 初期化：UEFI のメモリマップを与える */
void page_allocator_init(const MEMORY_MAP* map);
void page_allocator_release_boot_services_data(const MEMORY_MAP* map);


/* 任意サイズ（バイト単位）確保/解放（ページ境界に切り上げ） */
void* page_alloc_bytes(size_t nbytes);
void  page_free_bytes(void* ptr, size_t nbytes);

/* ページ数とアラインメント（>= PAGE_SIZE）を指定して確保 */
void* page_alloc_pages(size_t num_pages, size_t align_bytes);
void* page_alloc_4k_aligned(void);
void  page_free_4k(void* ptr);
