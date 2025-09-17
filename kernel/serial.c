// kernel/serial.c
#include "serial.h"
#include "arch_x86_io.h"

/* UART レジスタの COM ベースからのオフセット */
enum {
    REG_THR_RBR_DLL = 0, /* 送信保持/受信バッファ/Divisor Low (DLAB=1) */
    REG_IER_DLM     = 1, /* 割込許可/Divisor High (DLAB=1) */
    REG_IIR_FCR     = 2, /* 割込識別/ FIFO 制御 */
    REG_LCR         = 3, /* ライン制御 (DLAB はここの bit7) */
    REG_MCR         = 4, /* モデム制御 */
    REG_LSR         = 5, /* ライン状態 */
    REG_MSR         = 6, /* モデム状態 */
    REG_SCR         = 7  /* スクラッチ */
};

/* LSR bits */
#define LSR_THRE  (1u << 5)  /* Transmitter Holding Register Empty */

/* 既定 UART 基準クロック */
#define UART_CLOCK 115200u

/* 内部：送出 1 バイト（THR 空き待ち→書き込み） */
static void serial_write_byte_impl(serial_device_t* dev, uint8_t byte) {
    /* THR が空くまで待機 */
    while ((inb(dev->base + REG_LSR) & LSR_THRE) == 0)
        cpu_relax();

    outb(dev->base + REG_THR_RBR_DLL, byte);
}

void serial_init(serial_device_t* dev, serial_port_t port, uint32_t baud) {
    if (!dev) return;
    if (baud == 0) baud = UART_CLOCK; /* 既定: 115200 */

    dev->base = (uint16_t)port;
    dev->baud = baud;

    /* 1) 8N1 設定（DLAB=0 の状態で LCR=0x03） */
    outb(dev->base + REG_LCR, 0x03);      /* 8 data, 1 stop, no parity */

    /* 2) 割込みは使わない（ポーリング） */
    outb(dev->base + REG_IER_DLM, 0x00);

    /* 3) FIFO は無効化（最小構成） */
    outb(dev->base + REG_IIR_FCR, 0x00);

    /* 4) ボーレート設定: divisor = 115200 / baud */
    uint32_t divisor = UART_CLOCK / baud;
    uint8_t lcr = inb(dev->base + REG_LCR);
    outb(dev->base + REG_LCR, lcr | 0x80);                    /* DLAB=1 */
    outb(dev->base + REG_THR_RBR_DLL, (uint8_t)(divisor & 0xFF));       /* DLL */
    outb(dev->base + REG_IER_DLM,     (uint8_t)((divisor >> 8) & 0xFF)); /* DLM */
    outb(dev->base + REG_LCR, lcr & (uint8_t)~0x80);          /* DLAB=0 に戻す */

    /* 5) 必要に応じて MCR を最低限セット（DTR/RTS）。なくても送出は可能 */
    outb(dev->base + REG_MCR, 0x03);

    /* デフォルトの関数実体をバインド */
    dev->write_byte = serial_write_byte_impl;
    dev->read_byte  = 0; /* 未実装 */
}

void serial_write_byte(serial_device_t* dev, uint8_t byte) {
    if (!dev || !dev->write_byte) return;
    dev->write_byte(dev, byte);
}

void serial_write(serial_device_t* dev, const char* s) {
    if (!dev || !s) return;
    for (const char* p = s; *p; ++p) {
        /* 行末は \r\n に正規化（QEMU の -serial stdio でも読みやすく） */
        if (*p == '\n') {
            serial_write_byte(dev, (uint8_t)'\r');
        }
        serial_write_byte(dev, (uint8_t)*p);
    }
}

void serial_writeln(serial_device_t* dev, const char* s) {
    serial_write(dev, s);
    serial_write(dev, "\r\n");
}
