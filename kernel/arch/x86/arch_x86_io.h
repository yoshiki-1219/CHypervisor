// kernel/arch_x86_io.h
#pragma once
#include <stdint.h>

/* CPU ヒント。スピン時の電力とバス競合を軽減 */
static inline void cpu_relax(void) {
    __asm__ __volatile__("pause");
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v; __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void io_wait(void) {
    /* 伝統的な I/O wait。適当なポートへ write（何でも可） */
    __asm__ __volatile__("outb %%al, $0x80" :: "a"(0));
}