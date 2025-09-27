#include "gdt.h"
#include "arch/x86/arch_x86_low.h"

#define GDT_MAX 0x10

/* GDT と GDTR */
static __attribute__((aligned(16))) uint64_t gdt[GDT_MAX];
static GdtRegister gdtr;

/* TSS (未使用でも TR != 0 にするため必須) */
static __attribute__((aligned(16))) Tss64 g_tss;

/* ==================== ヘルパ ==================== */

/* SegmentDescriptor → 64bit値に変換 */
static inline uint64_t segdesc_to_u64(SegmentDescriptor d) {
    return *(uint64_t*)&d;
}

/* コードセグメント (64bit) */
static SegmentDescriptor make_code64_desc_struct(void) {
    SegmentDescriptor d = {
        .limit_low   = 0xFFFF,
        .base_low    = 0,
        .accessed    = 0,
        .rw          = 1,  // Readable
        .dc          = 0,
        .executable  = 1,
        .desc_type   = DESCRIPTOR_CODE_DATA,
        .dpl         = 0,
        .present     = 1,
        .limit_high  = 0xF,
        .avl         = 0,
        .long_mode   = 1,  // 64bit code
        .db          = 0,  // must be 0 in 64bit code
        .granularity = GRANULARITY_KBYTE,
        .base_high   = 0,
    };
    return d;
}

/* データセグメント */
static SegmentDescriptor make_data_desc_struct(void) {
    SegmentDescriptor d = {
        .limit_low   = 0xFFFF,
        .base_low    = 0,
        .accessed    = 0,
        .rw          = 1,  // Writable
        .dc          = 0,
        .executable  = 0,
        .desc_type   = DESCRIPTOR_CODE_DATA,
        .dpl         = 0,
        .present     = 1,
        .limit_high  = 0xF,
        .avl         = 0,
        .long_mode   = 0,
        .db          = 1,  // 32bit opsize (ignored in 64bit mode)
        .granularity = GRANULARITY_KBYTE,
        .base_high   = 0,
    };
    return d;
}

/* TSS Descriptor 設定 */
static void set_tss_desc(uint16_t index, uint64_t base, uint32_t limit) {
    TssDescriptor desc = {
        .limit_low = (uint16_t)(limit & 0xFFFF),
        .base_low = (uint16_t)(base & 0xFFFF),
        .base_mid1 = (uint8_t)((base >> 16) & 0xFF),
        .type = 0x9, /* Available 64bit TSS */
        .zero = 0,
        .dpl = 0,
        .present = 1,
        .limit_high = (uint8_t)((limit >> 16) & 0xF),
        .avl = 0,
        .zero2 = 0,
        .granularity = 0,
        .base_mid2 = (uint8_t)((base >> 24) & 0xFF),
        .base_high = (uint32_t)((base >> 32) & 0xFFFFFFFF),
        .reserved = 0,
    };
    *(TssDescriptor*)&gdt[index] = desc;
}

/* ==================== 公開 API ==================== */

void gdt_init(void)
{
    /* 0: Null descriptor */
    gdt[0] = 0;

    /* 1: kernel data, 2: kernel code */
    gdt[KERNEL_DS_IDX] = segdesc_to_u64(make_data_desc_struct());
    gdt[KERNEL_CS_IDX] = segdesc_to_u64(make_code64_desc_struct());

    /* TSS 初期化 (I/O bitmap 無効化) */
    for (size_t i = 0; i < sizeof(g_tss)/sizeof(uint64_t); ++i) {
        ((uint64_t*)&g_tss)[i] = 0;
    }
    g_tss.iomap_base = (uint16_t)sizeof(g_tss);
    

    /* 3-4: TSS descriptor */
    set_tss_desc(KERNEL_TSS_IDX,
                 (uint64_t)(uintptr_t)&g_tss,
                 (uint32_t)(sizeof(g_tss) - 1));

    /* GDTR 設定 → LGDT */
    gdtr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtr.base  = (uint64_t)(uintptr_t)gdt;
    load_gdtr(&gdtr);

    /* セグメントレジスタ更新 */
    uint16_t ds_sel = sel_gdt(KERNEL_DS_IDX, 0);
    uint16_t cs_sel = sel_gdt(KERNEL_CS_IDX, 0);
    uint16_t ts_sel = sel_gdt(KERNEL_TSS_IDX, 0);

    load_ds_es_fs_gs_ss(ds_sel);
    load_cs(cs_sel);
    load_tr(ts_sel);
}
