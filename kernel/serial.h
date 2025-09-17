// kernel/serial.h
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 代表的な PC 互換 UART のベースポート */
typedef enum {
    COM1 = 0x3F8,
    COM2 = 0x2F8,
    COM3 = 0x3E8,
    COM4 = 0x2E8,
} serial_port_t;

typedef struct serial_device {
    uint16_t      base;      /* I/O base port */
    uint32_t      baud;      /* 設定済みボーレート */
    /* 必要なら関数ポインタで差し替え可能 */
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

#ifdef __cplusplus
}
#endif
