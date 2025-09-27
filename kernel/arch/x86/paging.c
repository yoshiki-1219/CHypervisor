#include <stdint.h>
#include "arch/x86/paging.h"
#include "page_alloc.h"
#include "common.h"
#include "arch/x86/arch_x86_low.h"
#include "memmap.h"
#include "log.h"

/* ---- テーブル型（512エントリ） ---- */
typedef PageTableEntry pte_t;
typedef pte_t          pt_t[PT_ENTRIES];

/* ---- 新しいLv4の先頭（CR3に書き込む） ---- */
static pt_t *g_new_lv4 = NULL;

/* ---- ユーティリティ ---- */
static inline uint16_t idx_lv4(uint64_t va) { return (va >> LV4_SHIFT) & PT_INDEX_MASK; }
static inline uint16_t idx_lv3(uint64_t va) { return (va >> LV3_SHIFT) & PT_INDEX_MASK; }
static inline uint16_t idx_lv2(uint64_t va) { return (va >> LV2_SHIFT) & PT_INDEX_MASK; }
static inline uint16_t idx_lv1(uint64_t va) { return (va >> LV1_SHIFT) & PT_INDEX_MASK; }

/* PTE から下位テーブル物理 → 仮想（Direct Mapで見る想定） */
static inline pt_t *pt_from_pte(PageTableEntry e) {
    uint64_t pa = (uint64_t)e.phys << 12;
    return (pt_t*)(paging_phys2virt(pa));
}

/* 4KiBページ確保（必須） */
static pt_t *alloc_pt_zeroed(void) {
    pt_t *p = (pt_t*)page_alloc_4k_aligned();
    if (!p) return NULL;
    memset(p, 0, sizeof(*p));
    return p;
}

/* Lv3（PDPTE）に 1GiB ページで Direct Map を作る */
static int map_direct_region_1g(pt_t *lv4)
{
    const uint16_t l4_start = idx_lv4(DIRECT_MAP_BASE);
    const uint16_t l4_end   = l4_start + (DIRECT_MAP_SIZE >> LV4_SHIFT);

    for (uint16_t l4 = l4_start; l4 < l4_end; ++l4) {
        pt_t *lv3 = alloc_pt_zeroed();
        if (!lv3) return -1;

        for (uint32_t l3 = 0; l3 < PT_ENTRIES; ++l3) {
            uint64_t pa = ((uint64_t)l3) << LV3_SHIFT;
            (*lv3)[l3] = PTE_FLAGS_1G;   /* 定型フラグ */
            (*lv3)[l3].phys = pa >> 12;  /* アドレス格納 */
        }

        uint64_t lv3_pa = paging_virt2phys((uint64_t)lv3);
        PageTableEntry e = {0};
        e.present = 1;
        e.rw = 1;
        e.global = 1;
        e.phys = lv3_pa >> 12;
        (*lv4)[l4] = e;
    }
    return 0;
}

/* ------- 既存カーネル領域のクローン ------- */

static pt_t *clone_lv1(pt_t *old_lv1)
{
    pt_t *new_lv1 = alloc_pt_zeroed();
    if (!new_lv1) return NULL;
    memcpy(new_lv1, old_lv1, sizeof(*new_lv1));
    return new_lv1;
}

static pt_t *clone_lv2(pt_t *old_lv2)
{
    pt_t *new_lv2 = alloc_pt_zeroed();
    if (!new_lv2) return NULL;
    memcpy(new_lv2, old_lv2, sizeof(*new_lv2));

    for (uint32_t i = 0; i < PT_ENTRIES; ++i) {
        PageTableEntry *pte = &(*new_lv2)[i];
        if (!pte->present) continue;

        if (!pte->ps) {
            pt_t *old_lv1 = pt_from_pte(*pte);
            pt_t *new_lv1 = clone_lv1(old_lv1);
            if (!new_lv1) return NULL;

            uint64_t pa = paging_virt2phys((uint64_t)new_lv1);
            pte->phys = pa >> 12;
        }
    }
    return new_lv2;
}

static pt_t *clone_lv3(pt_t *old_lv3)
{
    pt_t *new_lv3 = alloc_pt_zeroed();
    if (!new_lv3) return NULL;
    memcpy(new_lv3, old_lv3, sizeof(*new_lv3));

    for (uint32_t i = 0; i < PT_ENTRIES; ++i) {
        PageTableEntry *pte = &(*new_lv3)[i];
        if (!pte->present) continue;

        if (!pte->ps) {
            pt_t *old_lv2 = pt_from_pte(*pte);
            pt_t *new_lv2 = clone_lv2(old_lv2);
            if (!new_lv2) return NULL;

            uint64_t pa = paging_virt2phys((uint64_t)new_lv2);
            pte->phys = pa >> 12;
        }
    }
    return new_lv3;
}

static int clone_kernel_upper_half(pt_t *new_lv4)
{
    uint64_t old_cr3 = read_cr3();
    pt_t *old_lv4 = (pt_t*)paging_phys2virt(old_cr3 & PTE_ADDR_MASK);

    const uint16_t l4_start = idx_lv4(DIRECT_MAP_BASE) + (DIRECT_MAP_SIZE >> LV4_SHIFT);
    for (uint16_t l4 = l4_start; l4 < PT_ENTRIES; ++l4) {
        PageTableEntry pte = (*old_lv4)[l4];
        if (!pte.present) continue;

        pt_t *old_lv3 = pt_from_pte(pte);
        pt_t *new_lv3 = clone_lv3(old_lv3);
        if (!new_lv3) return -1;

        uint64_t pa = paging_virt2phys((uint64_t)new_lv3);
        PageTableEntry e = pte;
        e.phys = pa >> 12;
        (*new_lv4)[l4] = e;
    }
    return 0;
}

/* ------- 公開 API ------- */

int paging_reconstruct(void)
{
    g_new_lv4 = alloc_pt_zeroed();
    if (!g_new_lv4) return -1;

    if (map_direct_region_1g(g_new_lv4) != 0)
        return -1;

    if (clone_kernel_upper_half(g_new_lv4) != 0)
        return -1;

    uint64_t new_cr3 = paging_virt2phys((uint64_t)g_new_lv4) & PTE_ADDR_MASK;
    write_cr3(new_cr3);

    KLOG_INFO("paging", "CR3 switched: new_lv4=%p", (void*)g_new_lv4);
    return 0;
}

/* ------- 変換ヘルパ ------- */

static int g_mapping_reconstructed = 0;

uint64_t paging_virt2phys(uint64_t va)
{
    if (!g_mapping_reconstructed) {
        return va;
    }
    if (va < KERNEL_BASE) {
        return va - DIRECT_MAP_BASE;
    } else {
        return va - KERNEL_BASE;
    }
}

uint64_t paging_phys2virt(uint64_t pa)
{
    if (!g_mapping_reconstructed) {
        return pa;
    }
    return pa + DIRECT_MAP_BASE;
}

uint64_t virt2phys(uint64_t va) { return paging_virt2phys(va); }
uint64_t phys2virt(uint64_t pa) { return paging_phys2virt(pa); }

__attribute__((noinline))
static void paging_mark_reconstructed(void) { g_mapping_reconstructed = 1; }

int paging_reconstruct_and_mark(void)
{
    int rc = paging_reconstruct();
    if (rc == 0) paging_mark_reconstructed();
    return rc;
}
