// kernel/serial.c
#include "serial.h"
#include "arch_x86_io.h"

/* 内部：送出 1 バイト（THR 空き待ち→書き込み） */
static void serial_write_byte_impl(serial_device_t* dev, uint8_t byte) {
    if (!dev) return;

    /* THR が空くまで待機 (LSR の THRE ビット確認) */
    while ((inb(dev->base + REG_LSR) & LSR_THRE) == 0) {
        cpu_relax();
    }

    /* THR にデータを書き込む */
    outb(dev->base + REG_THR, byte);
}

/* UART 初期化 */
void serial_init(serial_device_t* dev, serial_port_t port, uint32_t baud) {
    if (!dev) return;
    if (baud == 0) baud = UART_CLOCK; /* デフォルト: 115200bps */

    dev->base = (uint16_t)port;
    dev->baud = baud;

    /* 1) 通信フォーマット設定: 8N1 (DLAB=0, LCR=0x03) */
    outb(dev->base + REG_LCR, 0x03);

    /* 2) 割込み無効化 (ポーリング運用) */
    outb(dev->base + REG_IER, 0x00);

    /* 3) FIFO 無効化 */
    outb(dev->base + REG_FCR, 0x00);

    /* 4) ボーレート設定: divisor = 115200 / baud */
    uint32_t divisor = UART_CLOCK / baud;
    uint8_t lcr = inb(dev->base + REG_LCR);

    /* DLAB=1 に切り替え */
    outb(dev->base + REG_LCR, lcr | 0x80);

    /* DLL / DLM に divisor を分割して書き込み */
    outb(dev->base + REG_DLL, (uint8_t)(divisor & 0xFF));
    outb(dev->base + REG_DLM, (uint8_t)((divisor >> 8) & 0xFF));

    /* DLAB=0 に戻す */
    outb(dev->base + REG_LCR, lcr & (uint8_t)~0x80);

    // /* 5) モデム制御 (最低限 DTR/RTS 有効化) */
    // outb(dev->base + REG_MCR, 0x03);

    /* 関数ポインタをバインド */
    dev->write_byte = serial_write_byte_impl;
    dev->read_byte  = 0; /* 未実装 */
}

/* 1 バイト送信 (ラッパ) */
void serial_write_byte(serial_device_t* dev, uint8_t byte) {
    if (!dev || !dev->write_byte) return;
    dev->write_byte(dev, byte);
}

/* 文字列送信 */
void serial_write(serial_device_t* dev, const char* s) {
    if (!dev || !s) return;
    for (const char* p = s; *p; ++p) {
        /* 改行は \r\n に正規化 */
        if (*p == '\n') {
            serial_write_byte(dev, '\r');
        }
        serial_write_byte(dev, (uint8_t)*p);
    }
}

/* 行送信 */
void serial_writeln(serial_device_t* dev, const char* s) {
    serial_write(dev, s);
    serial_write(dev, "\r\n");
}

