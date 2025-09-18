#pragma once
#include <stdint.h>

/* ── 8259A I/O ports ───────────────────────────────────────────── */
#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

/* ── ベクタオフセット（IRQ0..7 → 0x20..0x27, IRQ8..15 → 0x28..0x2F） ─ */
#define PIC_PRIMARY_OFFSET   0x20
#define PIC_SECONDARY_OFFSET (PIC_PRIMARY_OFFSET + 8)

/* ── IRQ ライン（Linux準拠の命名に近め） ───────────────────────── */
typedef enum {
    IRQ_TIMER        = 0,
    IRQ_KEYBOARD     = 1,
    IRQ_CASCADE      = 2,  /* slave 8259 がぶら下がる */
    IRQ_SERIAL2      = 3,
    IRQ_SERIAL1      = 4,
    IRQ_PARALLEL23   = 5,
    IRQ_FLOPPY       = 6,
    IRQ_PARALLEL1    = 7,
    IRQ_RTC          = 8,
    IRQ_ACPI         = 9,
    IRQ_OPEN1        = 10,
    IRQ_OPEN2        = 11,
    IRQ_MOUSE        = 12,
    IRQ_COPROC       = 13,
    IRQ_PRIMARY_ATA  = 14,
    IRQ_SECONDARY_ATA= 15,
} pic_irq_t;

/* ── API ───────────────────────────────────────────────────────── */
void pic_init(void);                    /* 再マップ＋全マスク */
void pic_set_mask(pic_irq_t irq);       /* その IRQ をマスク   */
void pic_clear_mask(pic_irq_t irq);     /* その IRQ をアンマスク */
void pic_notify_eoi(pic_irq_t irq);     /* Specific EOI (slave時は両方) */

/* 便利ヘルパ */
static inline int pic_irq_is_primary(pic_irq_t irq) { return ((int)irq) < 8; }
static inline uint8_t pic_irq_delta(pic_irq_t irq)  { return (uint8_t)(pic_irq_is_primary(irq) ? (int)irq : (int)irq - 8); }

/* 参考: IRR/ISR 読み出し（必要なら使う） */
uint8_t pic_read_irr_primary(void);
uint8_t pic_read_isr_primary(void);
uint8_t pic_read_irr_secondary(void);
uint8_t pic_read_isr_secondary(void);
