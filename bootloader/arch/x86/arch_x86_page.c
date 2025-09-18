// bootloader/arch_x86_page.c
#include "arch_x86_page.h"
#include "asm_x86.h"

#include <efi.h>
#include <efilib.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define PAGE_SHIFT_4K   12u
#define PAGE_SIZE_4K    (1ull << PAGE_SHIFT_4K)
#define PAGE_MASK_4K    (PAGE_SIZE_4K - 1)
#define ENTRIES_PER_PT  512u

// 物理/仮想アドレス表現
typedef uint64_t Phys;
typedef uint64_t Virt;

// ----- ページテーブルエントリ（各レベル同一レイアウト） -----
typedef struct __attribute__((packed)) {
    uint64_t present  : 1;  // P
    uint64_t rw       : 1;  // R/W
    uint64_t us       : 1;  // U/S
    uint64_t pwt      : 1;  // PWT
    uint64_t pcd      : 1;  // PCD
    uint64_t accessed : 1;  // A
    uint64_t dirty    : 1;  // D (page時のみ)
    uint64_t ps       : 1;  // PS (1=page, 0=table)
    uint64_t global   : 1;  // G (page時のみ)
    uint64_t ignored1 : 2;
    uint64_t restart  : 1;  // Ignored except HLAT
    uint64_t phys     : 51; // [51:12] 物理アドレス(4KiBアライン)
    uint64_t xd       : 1;  // NX (1=No-Execute)
} PtEntry;

typedef PtEntry Lv4Entry;
typedef PtEntry Lv3Entry;
typedef PtEntry Lv2Entry;
typedef PtEntry Lv1Entry;

// asm_x86.h 側で提供している想定
extern uint64_t read_cr3(void);
extern void     load_cr3(uint64_t cr3);

// 低位域は UEFI の恒等マップ前提で VA=PA とみなして直接参照
static inline PtEntry* phys_to_virt_tbl(Phys phys_page_base) {
    return (PtEntry*)(uintptr_t)(phys_page_base & ~PAGE_MASK_4K);
}

static inline Phys entry_addr(const PtEntry *e) {
    return ((Phys)e->phys) << PAGE_SHIFT_4K;
}

static inline void entry_set_phys(PtEntry *e, Phys p4k_aligned) {
    e->phys = (p4k_aligned >> PAGE_SHIFT_4K) & ((1ull<<51)-1);
}

static inline void clear_page(Phys phys_page) {
    volatile uint8_t *p = (uint8_t*)(uintptr_t)phys_page;
    for (UINTN i = 0; i < (UINTN)PAGE_SIZE_4K; ++i) p[i] = 0;
}

// ---- テーブル取得ヘルパ（物理アドレス→配列） ----
static inline Lv4Entry* get_lv4_table(Phys cr3) { return (Lv4Entry*)(uintptr_t)(cr3 & ~PAGE_MASK_4K); }
static inline Lv3Entry* get_lv3_table(Phys p)   { return (Lv3Entry*)(uintptr_t)(p   & ~PAGE_MASK_4K); }
static inline Lv2Entry* get_lv2_table(Phys p)   { return (Lv2Entry*)(uintptr_t)(p   & ~PAGE_MASK_4K); }
static inline Lv1Entry* get_lv1_table(Phys p)   { return (Lv1Entry*)(uintptr_t)(p   & ~PAGE_MASK_4K); }

// ---- インデックス算出 ----
static inline uint16_t idx_lv4(Virt v){ return (v >> 39) & 0x1FF; }
static inline uint16_t idx_lv3(Virt v){ return (v >> 30) & 0x1FF; }
static inline uint16_t idx_lv2(Virt v){ return (v >> 21) & 0x1FF; }
static inline uint16_t idx_lv1(Virt v){ return (v >> 12) & 0x1FF; }

// ---- 新規テーブル確保（BootServices->AllocatePages） ----
static EFI_STATUS alloc_new_table(PtEntry *dst_entry) {
    if (!dst_entry) return EFI_INVALID_PARAMETER;

    EFI_STATUS st;
    Phys page_phys = 0;

    st = uefi_call_wrapper(
        BS->AllocatePages, 4,
        AllocateAnyPages,
        EfiBootServicesData,
        1,
        (EFI_PHYSICAL_ADDRESS*)&page_phys
    );
    if (EFI_ERROR(st)) return st;

    clear_page(page_phys);

    PtEntry e = {0};
    e.present = 1;
    e.rw      = 1;
    e.us      = 0;
    e.ps      = 0; // table 参照
    entry_set_phys(&e, page_phys);

    *dst_entry = e;
    return EFI_SUCCESS;
}

// ---- Lv4 を writable にする（丸ごとコピー→CR3 差し替え） ----
EFI_STATUS page_set_lv4_writable(void) {
    EFI_STATUS st;
    Phys new_lv4_phys = 0;

    st = uefi_call_wrapper(
        BS->AllocatePages, 4,
        AllocateAnyPages,
        EfiBootServicesData,
        1,
        (EFI_PHYSICAL_ADDRESS*)&new_lv4_phys
    );
    if (EFI_ERROR(st)) return st;

    Lv4Entry *old_tbl = get_lv4_table(read_cr3());
    Lv4Entry *new_tbl = (Lv4Entry*)(uintptr_t)new_lv4_phys;

    for (UINTN i = 0; i < (UINTN)ENTRIES_PER_PT; ++i) {
        new_tbl[i] = old_tbl[i];
    }

    load_cr3(new_lv4_phys); // 全TLB flush
    return EFI_SUCCESS;
}

// ---- 属性の適用（4KiBページ前提） ----
static void pte_apply_attr(PtEntry *pte, PageAttr a) {
    switch (a) {
        case PAGE_RO:  pte->rw = 0; pte->xd = 1; break;
        case PAGE_RW:  pte->rw = 1; pte->xd = 1; break;
        case PAGE_RX:  pte->rw = 0; pte->xd = 0; break;
        case PAGE_RWX: pte->rw = 1; pte->xd = 0; break;
    }
}

static PtEntry* l1_entry_for_va(uint64_t va) {
    PtEntry *l4 = get_lv4_table(read_cr3());
    PtEntry e4 = l4[idx_lv4(va)];
    if (!e4.present) return NULL;

    PtEntry *l3 = get_lv3_table(entry_addr(&e4));
    PtEntry e3 = l3[idx_lv3(va)];
    if (!e3.present || e3.ps) return NULL;

    PtEntry *l2 = get_lv2_table(entry_addr(&e3));
    PtEntry e2 = l2[idx_lv2(va)];
    if (!e2.present || e2.ps) return NULL;

    PtEntry *l1 = get_lv1_table(entry_addr(&e2));
    return &l1[idx_lv1(va)];
}

EFI_STATUS page_set_attr_4k(uint64_t vaddr, PageAttr attr) {
    PtEntry *l1 = l1_entry_for_va(vaddr);
    if (!l1 || !l1->present || !l1->ps) return EFI_NOT_FOUND;
    pte_apply_attr(l1, attr);
    invlpg(vaddr);
    return EFI_SUCCESS;
}

EFI_STATUS page_apply_attr_range(uint64_t vaddr, uint64_t size, PageAttr attr) {
    uint64_t start = vaddr & ~0xFFFULL;
    uint64_t end   = (vaddr + size + 0xFFFULL) & ~0xFFFULL;
    for (uint64_t va = start; va < end; va += PAGE_SIZE_4K) {
        EFI_STATUS st = page_set_attr_4k(va, attr);
        if (EFI_ERROR(st)) return st;
    }
    return EFI_SUCCESS;
}

// ---- 4KiB ページをマップ ----
EFI_STATUS page_map4k_to(Virt virt, Phys phys, PageAttr attr)
{
    if ((virt & PAGE_MASK_4K) || (phys & PAGE_MASK_4K))
        return EFI_INVALID_PARAMETER;

    // Lv4 → Lv3 → Lv2 → Lv1 と降りる。無ければその場で確保。
    Lv4Entry *lv4 = get_lv4_table(read_cr3());
    PtEntry  *e4  = &lv4[idx_lv4(virt)];
    if (!e4->present) {
        EFI_STATUS st = alloc_new_table(e4);
        if (EFI_ERROR(st)) return st;
    }

    Lv3Entry *lv3 = get_lv3_table(entry_addr(e4));
    PtEntry  *e3  = &lv3[idx_lv3(virt)];
    if (!e3->present) {
        EFI_STATUS st = alloc_new_table(e3);
        if (EFI_ERROR(st)) return st;
    }

    Lv2Entry *lv2 = get_lv2_table(entry_addr(e3));
    PtEntry  *e2  = &lv2[idx_lv2(virt)];
    if (!e2->present) {
        EFI_STATUS st = alloc_new_table(e2);
        if (EFI_ERROR(st)) return st;
    }

    Lv1Entry *lv1 = get_lv1_table(entry_addr(e2));
    PtEntry  *e1  = &lv1[idx_lv1(virt)];

    if (e1->present && e1->ps) {
        // すでに 4KiB ページでマップ済み
        return EFI_ALREADY_STARTED;
    }

    PtEntry new_e1 = {0};
    new_e1.present = 1;
    new_e1.rw      = 1;   // 一旦 RW で立て、下で attr を反映
    new_e1.us      = 0;
    new_e1.ps      = 1;   // 1=page をマップ
    entry_set_phys(&new_e1, phys);
    pte_apply_attr(&new_e1, attr);

    *e1 = new_e1;
    // non-present -> present なので TLB 全体 flush 不要（必要なら invlpg でもOK）
    return EFI_SUCCESS;
}
