// kernel/serial.h
#pragma once
#include <stdint.h>

/* ================================
 * 代表的な PC/AT 互換 UART のベースポート
 * ================================ */
typedef enum {
    COM1 = 0x3F8,
    COM2 = 0x2F8,
    COM3 = 0x3E8,
    COM4 = 0x2E8,
} serial_port_t;

/* ================================
 * UART レジスタの COM ベースからのオフセット
 * DLAB ビット (LCR の bit7) により解釈が変わる
 * ================================ */
enum {
    REG_THR = 0, /* W: Transmitter Holding Register (DLAB=0) */
    REG_RBR = 0, /* R: Receiver Buffer Register (DLAB=0) */
    REG_DLL = 0, /* R/W: Divisor Latch Low (DLAB=1) */

    REG_IER = 1, /* R/W: Interrupt Enable Register (DLAB=0) */
    REG_DLM = 1, /* R/W: Divisor Latch High (DLAB=1) */

    REG_IIR = 2, /* R: Interrupt Identification Register */
    REG_FCR = 2, /* W: FIFO Control Register */

    REG_LCR = 3, /* R/W: Line Control Register */
    REG_MCR = 4, /* R/W: Modem Control Register */

    REG_LSR = 5, /* R: Line Status Register */
    REG_MSR = 6, /* R: Modem Status Register */
    REG_SCR = 7  /* R/W: Scratch Register */
};

/* ================================
 * Line Status Register (LSR) bits
 * ================================ */
#define LSR_DR   (1u << 0) /* Data Ready */
#define LSR_THRE (1u << 5) /* Transmitter Holding Register Empty */
#define LSR_TEMT (1u << 6) /* Transmitter Empty */

/* ================================
 * UART クロックと既定ボーレート
 * ================================ */
#define UART_CLOCK        115200u
#define UART_DEFAULT_BAUD 9600u

/* ================================
 * シリアルデバイス構造体 (任意)
 * ================================ */
typedef struct serial_device {
    uint16_t      base;      /* I/O base port */
    uint32_t      baud;      /* 設定済みボーレート */
    void (*write_byte)(struct serial_device*, uint8_t);
    int  (*read_byte)(struct serial_device*); /* 未実装なら負値 */
} serial_device_t;


/* 初期化：8N1 / 指定ボーレート（既定 115200） */
void serial_init(serial_device_t* dev, serial_port_t port, uint32_t baud);

/* 1 バイト送出（ポーリング） */
void serial_write_byte(serial_device_t* dev, uint8_t byte);

/* 文字列送出（'\0' 終端なし・長さ指定なし版：安全のため ASCII 前提） */
void serial_write(serial_device_t* dev, const char* s);

/* 換行付きの簡易出力 */
void serial_writeln(serial_device_t* dev, const char* s);