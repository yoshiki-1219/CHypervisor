#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CR3 読み書き */
static inline uint64_t x86_read_cr3(void) {
    uint64_t v; __asm__ __volatile__("mov %%cr3,%0" : "=r"(v)); return v;
}
static inline void x86_write_cr3(uint64_t v) {
    __asm__ __volatile__("mov %0,%%cr3" :: "r"(v) : "memory");
}

/* ページテーブルエントリのビット定義 */
enum {
    PTE_P   = 1ULL << 0,   /* present */
    PTE_W   = 1ULL << 1,   /* writable */
    PTE_U   = 1ULL << 2,   /* user */
    PTE_PWT = 1ULL << 3,
    PTE_PCD = 1ULL << 4,
    PTE_A   = 1ULL << 5,   /* accessed */
    PTE_D   = 1ULL << 6,   /* dirty (Lv1のみ意味) */
    PTE_PS  = 1ULL << 7,   /* page size (Lv2=2MiB, Lv3=1GiB) */
    PTE_G   = 1ULL << 8,   /* global */
    PTE_NX  = 1ULL << 63,  /* execute disable（CR4.PAE+EFER.NXE 前提） */
};

/* 1GiB（Lv3のPS=1）・2MiB（Lv2のPS=1）・4KiB（Lv1）の各フラグ例 */
#define PTE_FLAGS_4K   (PTE_P|PTE_W|PTE_A|PTE_D|PTE_G)
#define PTE_FLAGS_2M   (PTE_P|PTE_W|PTE_A|PTE_PS|PTE_G)
#define PTE_FLAGS_1G   (PTE_P|PTE_W|PTE_A|PTE_PS|PTE_G)

/* 4KiB ページ境界マスク（アドレス部） */
#define PTE_ADDR_MASK  0x000FFFFFFFFFF000ULL

/* 物理アドレス→PTE フィールド */
static inline uint64_t pte_from_pa(uint64_t pa) {
    return pa & PTE_ADDR_MASK;
}

/* 4KiB 物理ページの取得関数（ページアロケータ側で用意） */
void *page_alloc_4k_aligned(void);   /* 4KiBアライン・4KiBサイズ */
void  page_free_4k(void *p);         /* 必要なら実装（今回は未使用） */

/* 再マッピング本体 */
int paging_reconstruct(void);
int paging_reconstruct_and_mark(void);

/* 変換（仮想⇔物理） */
uint64_t paging_virt2phys(uint64_t va);
uint64_t paging_phys2virt(uint64_t pa);

uint64_t virt2phys(uint64_t va);
uint64_t phys2virt(uint64_t pa);

#ifdef __cplusplus
}
#endif
