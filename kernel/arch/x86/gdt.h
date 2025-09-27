#pragma once
#include <stdint.h>
#include <stddef.h>
#include "arch/x86/arch_x86_low.h"

/* ============================================================
 * GDT 定義
 * ============================================================ */

/* GDT entry indices */
enum {
    KERNEL_DS_IDX  = 0x01,  /* 0x08 */
    KERNEL_CS_IDX  = 0x02,  /* 0x10 */
    KERNEL_TSS_IDX = 0x03,  /* 0x18, ここから2エントリ使用 */
};

/* ディスクリプタタイプ */
typedef enum {
    DESCRIPTOR_SYSTEM    = 0,
    DESCRIPTOR_CODE_DATA = 1,
} DescriptorType;

/* グラニュラリティ */
typedef enum {
    GRANULARITY_BYTE  = 0,
    GRANULARITY_KBYTE = 1,
} Granularity;

/* セグメントセレクタ */
typedef struct __attribute__((packed)) {
    uint16_t rpl   : 2;  /* Requested Privilege Level */
    uint16_t ti    : 1;  /* Table Indicator (0=GDT, 1=LDT) */
    uint16_t index : 13; /* Index */
} SegmentSelector;

/* セグメントディスクリプタ (SDM Vol.3A 3.4.5) */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;        /* セグメントリミット下位16bit */
    uint32_t base_low : 24;    /* ベースアドレス下位24bit */

    uint32_t accessed   : 1;   /* アクセス済み */
    uint32_t rw         : 1;   /* 読み書き可能 */
    uint32_t dc         : 1;   /* ディレクション/コンフォーミング */
    uint32_t executable : 1;   /* 実行可能 */
    uint32_t desc_type  : 1;   /* ディスクリプタタイプ */
    uint32_t dpl        : 2;   /* Descriptor Privilege Level */
    uint32_t present    : 1;   /* Present */

    uint32_t limit_high : 4;   /* リミット上位4bit */
    uint32_t avl        : 1;   /* OS用 */
    uint32_t long_mode  : 1;   /* 64bitコードセグメント */
    uint32_t db         : 1;   /* Default operation size */
    uint32_t granularity: 1;   /* Granularity */
    uint32_t base_high  : 8;   /* ベースアドレス上位8bit */
} SegmentDescriptor;

/* GDTR (Global Descriptor Table Register) */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;  /* GDT のベースアドレス */
} GdtRegister;

/// TSS Descriptor (16バイト, 2エントリ専有)
typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid1;
    uint8_t  type : 4;
    uint8_t  zero : 1;
    uint8_t  dpl  : 2;
    uint8_t  present : 1;
    uint8_t  limit_high : 4;
    uint8_t  avl : 1;
    uint8_t  zero2 : 2;
    uint8_t  granularity : 1;
    uint8_t  base_mid2;
    uint32_t base_high;
    uint32_t reserved;
} TssDescriptor;

/* 64-bit TSS (最小構成) */
typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;  /* = sizeof(struct tss64) → I/O bitmap 無効 */
} Tss64;

/* ============================================================
 * セグメントレジスタ操作
 * ============================================================ */

/* GDTR をロード */
static inline void load_gdtr(const GdtRegister* g)
{
    __asm__ __volatile__("lgdt (%0)" :: "r"(g));
}

/* DS/ES/FS/GS/SS をロード */
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

/* CS は far return/jump が必要 (Long mode) */
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

/* セグメントセレクタ値生成 (TI=0 固定) */
static inline uint16_t sel_gdt(uint16_t index, uint16_t rpl)
{
    return (uint16_t)((index << 3) | (rpl & 0x3));
}


/* ============================================================
 * 外部公開関数
 * ============================================================ */

/* GDT 初期化 */
void gdt_init(void);
