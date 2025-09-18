#pragma once
#include <stdint.h>

static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline void load_cr3(uint64_t cr3) {
    __asm__ __volatile__("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

static inline void invlpg(uint64_t va) {
    __asm__ __volatile__("invlpg (%0)" :: "r"(va) : "memory");
}