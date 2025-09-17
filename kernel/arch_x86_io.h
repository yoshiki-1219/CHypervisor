// kernel/arch_x86_io.h
#pragma once
#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* CPU ヒント。スピン時の電力とバス競合を軽減 */
static inline void cpu_relax(void) {
    __asm__ __volatile__("pause");
}
