#pragma once
#include <stdint.h>
#include <stddef.h>

/* IDT のサイズ（x86-64 は 256 固定） */
enum { IDT_MAX_GATES = 256 };

/* 64-bit IDT Gate Descriptor (Interrupt/Trap) */
typedef struct __attribute__((packed)) idt_gate {
    uint16_t offset_low;     /* handler[15:0]   */
    uint16_t selector;       /* CS selector     */
    uint8_t  ist : 3;        /* IST (未使用なら0) */
    uint8_t  rsv1: 5;        /* 0               */
    uint8_t  type: 4;        /* 0xE:interrupt, 0xF:trap */
    uint8_t  zero: 1;        /* 0               */
    uint8_t  dpl : 2;        /* コール許可 DPL  */
    uint8_t  p   : 1;        /* Present         */
    uint16_t offset_mid;     /* handler[31:16]  */
    uint32_t offset_high;    /* handler[63:32]  */
    uint32_t rsv2;           /* 0               */
} idt_gate_t;

/* IDTR */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idtr_t;

/* 外部公開 API */
void idt_init(void);                       /* IDT 配列を LIDT する */
void idt_set_gate(int vec, void (*isr)(void),
                  uint16_t cs_selector,    /* 例: sel_gdt(KERNEL_CS_IDX, 0) (GDT実装) */
                  uint8_t gate_type,       /* 0xE: interrupt gate を推奨 */
                  uint8_t dpl);            /* 0 (Ring0) 推奨 */

/* 後述の共通 ISR エントリ（C 側ディスパッチャ呼び出し） */
void isr_common(void) __attribute__((naked));
