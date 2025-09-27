#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ページテーブルエントリ構造体（64bit） */
typedef struct {
    uint64_t present : 1;   /* P = Present */
    uint64_t rw      : 1;   /* W = Writable */
    uint64_t us      : 1;   /* U = User/Supervisor */
    uint64_t pwt     : 1;   /* PWT = Page-level write-through */
    uint64_t pcd     : 1;   /* PCD = Cache disable */
    uint64_t accessed: 1;   /* A = Accessed */
    uint64_t dirty   : 1;   /* D = Dirty (Lv1のみ意味) */
    uint64_t ps      : 1;   /* PS = Page size (Lv2=2MiB, Lv3=1GiB) */
    uint64_t global  : 1;   /* G = Global */
    uint64_t ignored1: 3;   /* 予約/未使用ビット */

    uint64_t phys    : 40;  /* 物理アドレス（4KiB境界まで） */
    uint64_t ignored2: 11;  /* OS管理用に使える領域など */
    uint64_t nx      : 1;   /* NX = Execute disable */
} __attribute__((packed)) PageTableEntry;

/* 定数: ページサイズごとのフラグ組み合わせ */
#define PTE_FLAGS_4K   ((PageTableEntry){ .present=1, .rw=1, .accessed=1, .dirty=1, .global=1 })
#define PTE_FLAGS_2M   ((PageTableEntry){ .present=1, .rw=1, .accessed=1, .ps=1, .global=1 })
#define PTE_FLAGS_1G   ((PageTableEntry){ .present=1, .rw=1, .accessed=1, .ps=1, .global=1 })

/* アドレスマスク */
#define PTE_ADDR_MASK  0x000FFFFFFFFFF000ULL

/* 物理アドレスをエントリに詰める関数 */
static inline PageTableEntry pte_from_pa(uint64_t pa) {
    PageTableEntry e = {0};
    e.phys = (pa & PTE_ADDR_MASK) >> 12;  // 12bit右シフトして格納
    e.present = 1;
    return e;
}

/* ページアロケータ（外部で実装） */
void *page_alloc_4k_aligned(void); /* 4KiB アライン・4KiBサイズ */
void  page_free_4k(void *p);       /* 必要なら実装 */

/* 再マッピング関連 */
int paging_reconstruct(void);
int paging_reconstruct_and_mark(void);

/* アドレス変換 */
uint64_t paging_virt2phys(uint64_t va);
uint64_t paging_phys2virt(uint64_t pa);

uint64_t virt2phys(uint64_t va);
uint64_t phys2virt(uint64_t pa);
