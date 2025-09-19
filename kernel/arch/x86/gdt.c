#include "gdt.h"
#include "arch/x86/arch_x86_low.h"

/* ===== GDT の内部表現 =====
 * 通常ディスクリプタ：64bit（u64）
 * TSS/LDT ディスクリプタ：128bit（連続する2エントリを使用）
 */
#define GDT_MAX  8

static __attribute__((aligned(16))) uint64_t gdt[GDT_MAX];

/* GDTR */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdtr_t;

static gdtr_t gdtr;

/* 未使用でも TR!=0 のために TSS を用意 */
static __attribute__((aligned(16))) tss64_t g_tss;

/* ===== ディスクリプタ生成ヘルパ =====
 * access: P|DPL|S|Type (0..7)
 * flags_nib: [G|D/B|L|AVL] を 1 nibble（bit3..0）で与える
 */
static uint64_t make_desc(uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t flags_nib)
{
    uint64_t d = 0;
    d  =  (uint64_t)(limit & 0xFFFFu);
    d |= ((uint64_t)(base  & 0xFFFFFFu)) << 16;
    d |= ((uint64_t)access)               << 40;
    d |= ((uint64_t)((limit >> 16) & 0xFu)) << 48;
    d |= ((uint64_t)(flags_nib & 0xFu))   << 52;
    d |= ((uint64_t)((base  >> 24) & 0xFFu)) << 56;
    return d;
}

/* コード/データ（Long mode の定番値）
 *  - Limit は無視されるが慣例で 0xFFFFF、G=1 にしておく
 *  - Code: access=0x9A (P=1,S=1,Type=0xA[Exec|Read])
 *          flags_nib=0xA (G=1,DB=0,L=1,AVL=0)
 *  - Data: access=0x92 (P=1,S=1,Type=0x2[Read/Write])
 *          flags_nib=0xC (G=1,DB=1,L=0,AVL=0)
 */
static uint64_t make_code64_desc(void) {
    return make_desc(0, 0xFFFFF, 0x9A, 0xA);
}
static uint64_t make_data_desc(void) {
    return make_desc(0, 0xFFFFF, 0x92, 0xC);
}

/* TSS ディスクリプタ（64-bit, Available=0x9）
 *  - Limit: sizeof(TSS)-1
 *  - flags_nib: G=0, DB=0, L=0, AVL=0 → 0x0
 *  - 上位 Qword に base[63:32] を格納（下位32bit）。上位32bit は 0。
 */
static void set_tss_desc(uint16_t index, uint64_t base, uint32_t limit)
{
    const uint8_t type = 0x9;            /* Available TSS */
    const uint8_t present = 1;
    uint8_t access = (uint8_t)((present << 7) | (0/*DPL*/ << 5) | (0/*S*/ << 4) | (type & 0xF));
    uint8_t flags  = 0x0;

    uint32_t base_lo  = (uint32_t)(base & 0xFFFFFFFFull);
    uint32_t base_hi  = (uint32_t)((base >> 32) & 0xFFFFFFFFull);

    uint64_t low = 0;
    low  =  (uint64_t)(limit & 0xFFFFu);
    low |= ((uint64_t)(base_lo  & 0xFFFFFFu)) << 16;
    low |= ((uint64_t)access)                 << 40;
    low |= ((uint64_t)((limit >> 16) & 0xFu)) << 48;
    low |= ((uint64_t)flags & 0xFu)           << 52;
    low |= ((uint64_t)((base_lo >> 24) & 0xFFu)) << 56;

    uint64_t high = (uint64_t)base_hi; /* 下位32bitに base[63:32]、上位32bit=0 */

    gdt[index]     = low;
    gdt[index + 1] = high;
}

// 例: 64bit TSS の base を取るヘルパを用意
uint64_t tss_base_from_gdt(uint16_t tr, struct desc_ptr gdtr) {
    const uint64_t gdt = gdtr.base;
    const uint64_t idx = (tr & ~0x7) /*RPL/TI除去*/;
    const uint64_t *desc = (uint64_t*)(gdt + idx);
    uint64_t low  = desc[0];
    uint64_t high = desc[1];
    uint64_t base = ((low  >> 16) & 0xFFFFFF)
                  | ((high & 0xFF) << 24)
                  | (high & 0xFFFFFFFF00000000ULL);
    return base;
}

/* ===== セグメントレジスタ更新 ===== */

static inline void load_gdtr(const gdtr_t* g)
{
    __asm__ __volatile__("lgdt (%0)" :: "r"(g));
}

static inline void load_ds_es_fs_gs_ss(uint16_t sel)
{
    __asm__ __volatile__(
        "mov %0, %%ds \n\t"
        "mov %0, %%es \n\t"
        "mov %0, %%fs \n\t"
        "mov %0, %%gs \n\t"
        "mov %0, %%ss \n\t"
        :: "r"(sel) : "memory");
}

/* CS は far return/jump が必要（Long mode）*/
static inline void load_cs(uint16_t sel)
{
    __asm__ __volatile__ (
        "pushq %[sel]     \n\t"
        "leaq  1f(%%rip), %%rax \n\t"
        "pushq %%rax      \n\t"
        "lretq            \n\t"
        "1:               \n\t"
        :: [sel]"r"((uint64_t)sel)
        : "rax","memory");
}

/* TR に TSS セレクタをロード */
static inline void load_tr(uint16_t sel)
{
    __asm__ __volatile__("ltr %0" :: "r"(sel));
}

/* ===== 公開：初期化 ===== */
void gdt_init(void)
{
    /* 0: NULL */
    gdt[0] = 0;

    /* 1: kernel data, 2: kernel code */
    gdt[KERNEL_DS_IDX] = make_data_desc();
    gdt[KERNEL_CS_IDX] = make_code64_desc();

    /* TSS 本体を最小初期化（I/O bitmap 無効化） */
    for (size_t i = 0; i < sizeof(g_tss)/sizeof(uint64_t); ++i) {
        ((uint64_t*)&g_tss)[i] = 0;
    }
    g_tss.iomap_base = (uint16_t)sizeof(g_tss);

    /* 3-4: TSS descriptor（2エントリ使用） */
    set_tss_desc(KERNEL_TSS_IDX, (uint64_t)(uintptr_t)&g_tss, (uint32_t)(sizeof(g_tss) - 1));

    /* GDTR 設定 → LGDT */
    gdtr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtr.base  = (uint64_t)(uintptr_t)gdt;
    load_gdtr(&gdtr);

    /* セグメントレジスタの Hidden Part を更新 */
    uint16_t ds_sel = sel_gdt(KERNEL_DS_IDX, 0);
    uint16_t cs_sel = sel_gdt(KERNEL_CS_IDX, 0);
    uint16_t ts_sel = sel_gdt(KERNEL_TSS_IDX, 0);

    load_ds_es_fs_gs_ss(ds_sel);
    load_cs(cs_sel);
    load_tr(ts_sel);
}
