#include "arch/x86/pic.h"
#include "arch/x86/arch_x86_io.h"


/* ── ICW/OCW 定数 ──────────────────────────────────────────────── */
#define ICW1_INIT      0x10    /* 初期化シーケンス開始 */
#define ICW1_ICW4      0x01    /* ICW4 必要 */
#define ICW4_8086      0x01    /* 8086/88 モード */
#define OCW2_EOI       0x20
#define OCW3_READ_IRR  0x0A
#define OCW3_READ_ISR  0x0B

static inline void pic_set_imr_primary(uint8_t imr) { outb(PIC1_DATA, imr); }
static inline void pic_set_imr_secondary(uint8_t imr){ outb(PIC2_DATA, imr); }

/* ── 初期化：再マップ、カスケード設定、ICW4 設定、全マスク ─────────── */
void pic_init(void)
{
    uint8_t a1 = inb(PIC1_DATA); /* 既存 IMR 保存（不要なら捨てても可） */
    uint8_t a2 = inb(PIC2_DATA);

    /* ICW1: 初期化 + ICW4 有り */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4); io_wait();

    /* ICW2: ベクタオフセット */
    outb(PIC1_DATA, PIC_PRIMARY_OFFSET);   io_wait();
    outb(PIC2_DATA, PIC_SECONDARY_OFFSET); io_wait();

    /* ICW3: カスケード設定
       - primary 側: IRQ2(ビット2=1) に secondary が接続
       - secondary 側: ID=2 を通知
     */
    outb(PIC1_DATA, 0x04); io_wait(); /* 0b0000_0100 */
    outb(PIC2_DATA, 0x02); io_wait();

    /* ICW4: 8086/88 モード */
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    /* IMR: とりあえず全マスク */
    (void)a1; (void)a2;
    pic_set_imr_primary(0xFF);
    pic_set_imr_secondary(0xFF);
}

/* ── マスク制御 ──────────────────────────────────────────────── */
void pic_set_mask(pic_irq_t irq)
{
    if (pic_irq_is_primary(irq)) {
        uint8_t imr = inb(PIC1_DATA);
        imr |= (1u << pic_irq_delta(irq));
        outb(PIC1_DATA, imr);
    } else {
        uint8_t imr = inb(PIC2_DATA);
        imr |= (1u << pic_irq_delta(irq));
        outb(PIC2_DATA, imr);
    }
}

void pic_clear_mask(pic_irq_t irq)
{
    if (pic_irq_is_primary(irq)) {
        uint8_t imr = inb(PIC1_DATA);
        imr &= ~(1u << pic_irq_delta(irq));
        outb(PIC1_DATA, imr);
    } else {
        uint8_t imr = inb(PIC2_DATA);
        imr &= ~(1u << pic_irq_delta(irq));
        outb(PIC2_DATA, imr);
    }
}

/* ── Specific EOI ─────────────────────────────────────────────── */
void pic_notify_eoi(pic_irq_t irq)
{
    if (pic_irq_is_primary(irq)) {
        /* primary なら primary に EOI */
        outb(PIC1_CMD, OCW2_EOI | pic_irq_delta(irq));
    } else {
        /* secondary のとき：
           1) secondary に対してその IRQ の EOI
           2) primary の IRQ2 にも EOI（カスケード元を明示）
         */
        outb(PIC2_CMD, OCW2_EOI | pic_irq_delta(irq));
        outb(PIC1_CMD, OCW2_EOI | 2); /* IRQ2 */
    }
}

/* ── IRR/ISR 読み出し（デバッグ用）────────────────────────────── */
static inline uint8_t pic_read_reg(uint16_t cmd_port, uint8_t ocw3)
{
    outb(cmd_port, ocw3);
    return inb(cmd_port);
}

uint8_t pic_read_irr_primary(void)    { return pic_read_reg(PIC1_CMD, OCW3_READ_IRR); }
uint8_t pic_read_isr_primary(void)    { return pic_read_reg(PIC1_CMD, OCW3_READ_ISR); }
uint8_t pic_read_irr_secondary(void)  { return pic_read_reg(PIC2_CMD, OCW3_READ_IRR); }
uint8_t pic_read_isr_secondary(void)  { return pic_read_reg(PIC2_CMD, OCW3_READ_ISR); }
