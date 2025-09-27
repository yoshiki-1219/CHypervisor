#pragma once
#include <stdint.h>

/// CPU を短時間スリープ (spinlock 等で使用)
static inline void cpu_relax(void) {
    __asm__ __volatile__("rep; nop");
}

static inline void io_wait(void) {
    __asm__ __volatile__("outb %%al, $0x80" :: "a"(0));
}

/// 1バイト出力
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" :: "a"(val), "Nd"(port));
}

/// 2バイト出力
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ __volatile__("outw %0, %1" :: "a"(val), "Nd"(port));
}

/// 4バイト出力
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ __volatile__("outl %0, %1" :: "a"(val), "Nd"(port));
}

/// 1バイト入力
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/// 2バイト入力
static inline uint16_t inw(uint16_t port) {
    uint16_t v;
    __asm__ __volatile__("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/// 4バイト入力
static inline uint32_t inl(uint16_t port) {
    uint32_t v;
    __asm__ __volatile__("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void cli() {
    __asm__ volatile("cli");
}

static inline void sti() {
    __asm__ volatile("sti");
}
