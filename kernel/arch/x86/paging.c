#include <stdint.h>
#include <string.h>
#include "arch/x86/paging.h"
#include "arch/x86/arch_x86_low.h"
#include "memmap.h"
#include "log.h"

/* ---- テーブル型（512エントリ） ---- */
typedef uint64_t pte_t;
typedef pte_t    pt_t[PT_ENTRIES];

/* ---- 新しいLv4の先頭（CR3に書き込む） ---- */
static pt_t *g_new_lv4 = NULL;

/* ---- ユーティリティ ---- */
static inline uint16_t idx_lv4(uint64_t va) { return (va >> LV4_SHIFT) & PT_INDEX_MASK; }
static inline uint16_t idx_lv3(uint64_t va) { return (va >> LV3_SHIFT) & PT_INDEX_MASK; }
static inline uint16_t idx_lv2(uint64_t va) { return (va >> LV2_SHIFT) & PT_INDEX_MASK; }
static inline uint16_t idx_lv1(uint64_t va) { return (va >> LV1_SHIFT) & PT_INDEX_MASK; }

/* PTE から下位テーブル物理 → 仮想（Direct Mapで見る想定） */
static inline pt_t *pt_from_pte(uint64_t pte) {
    uint64_t pa = pte & PTE_ADDR_MASK;
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
    /* DIRECT_MAP は 512GiB ちょうどなので、Lv4 は1エントリだけ使う */
    const uint16_t l4_start = idx_lv4(DIRECT_MAP_BASE);
    const uint16_t l4_end   = l4_start + (DIRECT_MAP_SIZE >> LV4_SHIFT); /* == l4_start+1 */

    for (uint16_t l4 = l4_start; l4 < l4_end; ++l4) {
        pt_t *lv3 = alloc_pt_zeroed();
        if (!lv3) return -1;

        /* Lv3: 各エントリで 1GiB PS=1 の物理領域を張る
           va = DIRECT_MAP_BASE + (l3 * 1GiB)
           pa = (l3 * 1GiB) */
        for (uint32_t l3 = 0; l3 < PT_ENTRIES; ++l3) {
            uint64_t pa = ((uint64_t)l3) << LV3_SHIFT; /* 0,1GiB,2GiB,... */
            (*lv3)[l3] = pte_from_pa(pa) | PTE_FLAGS_1G;
        }

        /* Lv4 から Lv3 を指す */
        uint64_t lv3_pa = paging_virt2phys((uint64_t)lv3);
        (*lv4)[l4] = pte_from_pa(lv3_pa) | PTE_P | PTE_W | PTE_A | PTE_G | PTE_NX;
    }
    return 0;
}

/* ------- 既存カーネル領域のクローン（上位ハーフ） ------- */

static pt_t *clone_lv1(pt_t *old_lv1)
{
    pt_t *new_lv1 = alloc_pt_zeroed();
    if (!new_lv1) return NULL;
    /* Lv1 は常に 4KiB ページを張る実体。丸ごとコピーで良い */
    memcpy(new_lv1, old_lv1, sizeof(*new_lv1));
    return new_lv1;
}

static pt_t *clone_lv2(pt_t *old_lv2)
{
    pt_t *new_lv2 = alloc_pt_zeroed();
    if (!new_lv2) return NULL;
    memcpy(new_lv2, old_lv2, sizeof(*new_lv2));

    for (uint32_t i = 0; i < PT_ENTRIES; ++i) {
        uint64_t pte = (*new_lv2)[i];
        if (!(pte & PTE_P)) continue;

        /* Lv2 エントリが「テーブル参照（PS=0）」なら再帰クローン */
        if (!(pte & PTE_PS)) {
            pt_t *old_lv1 = pt_from_pte(pte);
            pt_t *new_lv1 = clone_lv1(old_lv1);
            if (!new_lv1) return NULL;

            uint64_t pa = paging_virt2phys((uint64_t)new_lv1);
            (*new_lv2)[i] = pte_from_pa(pa) | (pte & ~PTE_ADDR_MASK);
        }
        /* PS=1（2MiBマップ）はそのままコピー済み */
    }
    return new_lv2;
}

static pt_t *clone_lv3(pt_t *old_lv3)
{
    pt_t *new_lv3 = alloc_pt_zeroed();
    if (!new_lv3) return NULL;
    memcpy(new_lv3, old_lv3, sizeof(*new_lv3));

    for (uint32_t i = 0; i < PT_ENTRIES; ++i) {
        uint64_t pte = (*new_lv3)[i];
        if (!(pte & PTE_P)) continue;

        /* Lv3 エントリが「テーブル参照（PS=0）」なら再帰クローン */
        if (!(pte & PTE_PS)) {
            pt_t *old_lv2 = pt_from_pte(pte);
            pt_t *new_lv2 = clone_lv2(old_lv2);
            if (!new_lv2) return NULL;

            uint64_t pa = paging_virt2phys((uint64_t)new_lv2);
            (*new_lv3)[i] = pte_from_pa(pa) | (pte & ~PTE_ADDR_MASK);
        }
        /* PS=1（1GiBマップ）はそのままコピー済み */
    }
    return new_lv3;
}

static int clone_kernel_upper_half(pt_t *new_lv4)
{
    /* 旧 CR3 の Lv4 を辿る（UEFI が設定したもの） */
    uint64_t old_cr3 = read_cr3();
    pt_t *old_lv4 = (pt_t*)paging_phys2virt(old_cr3 & PTE_ADDR_MASK);

    /* DirectMap の直後（= 上位ハーフ全域）を対象にクローン */
    const uint16_t l4_start = idx_lv4(DIRECT_MAP_BASE) + (DIRECT_MAP_SIZE >> LV4_SHIFT); /* 通常 +1 */
    for (uint16_t l4 = l4_start; l4 < PT_ENTRIES; ++l4) {
        uint64_t pte = (*old_lv4)[l4];
        if (!(pte & PTE_P)) continue;     /* 未使用エントリは飛ばす */

        /* old Lv4 が指す Lv3 テーブルを再帰クローン */
        pt_t *old_lv3 = pt_from_pte(pte);
        pt_t *new_lv3 = clone_lv3(old_lv3);
        if (!new_lv3) return -1;

        uint64_t pa = paging_virt2phys((uint64_t)new_lv3);
        (*new_lv4)[l4] = pte_from_pa(pa) | (pte & ~PTE_ADDR_MASK);
    }
    return 0;
}

/* ------- 公開 API ------- */

int paging_reconstruct(void)
{
    /* 新規 Lv4 を確保・ゼロ初期化 */
    g_new_lv4 = alloc_pt_zeroed();
    if (!g_new_lv4) return -1;

    /* Direct Map Region：Lv3 の 1GiB ページで張る */
    if (map_direct_region_1g(g_new_lv4) != 0)
        return -1;

    /* 旧ページテーブルから上位のカーネル領域をクローン */
    if (clone_kernel_upper_half(g_new_lv4) != 0)
        return -1;

    /* CR3 切替（TLB は暗黙にフラッシュされる） */
    uint64_t new_cr3 = paging_virt2phys((uint64_t)g_new_lv4) & PTE_ADDR_MASK;
    write_cr3(new_cr3);

    KLOG_INFO("paging", "CR3 switched: new_lv4=%p", (void*)g_new_lv4);
    return 0;
}

/* ------- 変換ヘルパ（簡易版） ------- */

static int g_mapping_reconstructed = 0;

uint64_t paging_virt2phys(uint64_t va)
{
    if (!g_mapping_reconstructed) {
        /* 置換前＝UEFI のストレートマップ扱い（仮想==物理） */
        return va;
    }

    if (va < KERNEL_BASE) {
        /* Direct Map: va = DIRECT_MAP_BASE + pa */
        return va - DIRECT_MAP_BASE;
    } else {
        /* Kernel image 領域: va = KERNEL_BASE + pa */
        return va - KERNEL_BASE;
    }
}

uint64_t paging_phys2virt(uint64_t pa)
{
    if (!g_mapping_reconstructed) {
        return pa;
    }
    /* 物理 → DirectMap の仮想に寄せる */
    return pa + DIRECT_MAP_BASE;
}

uint64_t virt2phys(uint64_t va) { return paging_virt2phys(va); }
uint64_t phys2virt(uint64_t pa) { return paging_phys2virt(pa); }

/* mapper 完了マークを立てる関数（main から呼ぶ） */
__attribute__((noinline))
static void paging_mark_reconstructed(void) { g_mapping_reconstructed = 1; }

/* 外からまとめて呼びたい場合のラッパ */
int paging_reconstruct_and_mark(void)
{
    int rc = paging_reconstruct();
    if (rc == 0) paging_mark_reconstructed();
    return rc;
}
