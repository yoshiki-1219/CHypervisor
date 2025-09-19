#pragma once
#include <stdint.h>
#include <stddef.h>
#include "arch/x86/arch_x86_low.h"
/* GDT entry index */
enum {
    KERNEL_DS_IDX  = 0x01,   /* 0x08 */
    KERNEL_CS_IDX  = 0x02,   /* 0x10 */
    KERNEL_TSS_IDX = 0x03,   /* 0x18, ここから2エントリ使用（0x18/0x20） */
};

static inline uint16_t sel_gdt(uint16_t index, uint16_t rpl) {
    return (uint16_t)((index << 3) | (rpl & 0x3)); /* TI=0(GDT) 固定 */
}

uint64_t tss_base_from_gdt(uint16_t tr, struct desc_ptr gdtr);

/* 64-bit TSS（最小構成） */
typedef struct __attribute__((packed)) tss64 {
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
    uint16_t iomap_base; /* = sizeof(struct tss64) で I/O bitmap 無効 */
} tss64_t;

/* 初期化：GDT 構築 → LGDT → DS/ES/FS/GS/SS/CS 更新 → LTR */
void gdt_init(void);
