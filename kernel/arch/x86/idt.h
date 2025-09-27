#pragma once
#include <stdint.h>
#include <stddef.h>


/* 64-bit IDT Gate Descriptor (Interrupt/Trap Gate) */
typedef struct __attribute__((packed)) {
    uint16_t offset_low;     /* handler[15:0]   */
    uint16_t selector;       /* CS セレクタ     */
    uint8_t  ist     : 3;    /* IST (未使用なら0) */
    uint8_t  reserved1 : 5;  /* 0 */
    uint8_t  type    : 4;    /* 0xE=interrupt, 0xF=trap */
    uint8_t  zero    : 1;    /* 0 */
    uint8_t  dpl     : 2;    /* Descriptor Privilege Level */
    uint8_t  present : 1;    /* Present */
    uint16_t offset_mid;     /* handler[31:16] */
    uint32_t offset_high;    /* handler[63:32] */
    uint32_t reserved2;      /* 0 */
} IdtGate;

/* IDTR (IDT Register) */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} IdtRegister;

/* lidt 命令ヘルパ */
static inline void load_idtr(const IdtRegister* idtr) {
    __asm__ __volatile__("lidt (%0)" :: "r"(idtr));
}

/* ===== 外部公開 API ===== */

/* IDT 初期化 (IDT 配列を構築し、LIDT を実行する) */
void idt_init(void);

/* IDT エントリを設定する */
void idt_set_entry(int vector, void (*isr)(void),
                   uint16_t cs_selector,   /* 例: sel_gdt(KERNEL_CS_IDX, 0) */
                   uint8_t gate_type,      /* 0xE: interrupt gate を推奨 */
                   uint8_t dpl);           /* 0 (Ring0) 推奨 */
