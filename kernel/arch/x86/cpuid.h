#pragma once
#include <stdint.h>

typedef struct {
    uint32_t eax, ebx, ecx, edx;
} cpuid_regs_t;

static inline cpuid_regs_t cpuid_ex(uint32_t leaf, uint32_t subleaf) {
    cpuid_regs_t r;
    __asm__ __volatile__("cpuid"
        : "=a"(r.eax), "=b"(r.ebx), "=c"(r.ecx), "=d"(r.edx)
        : "a"(leaf), "c"(subleaf));
    return r;
}

static inline cpuid_regs_t cpuid_leaf(uint32_t leaf) {
    return cpuid_ex(leaf, 0);
}

/* Vendor ID を 12バイト配列に格納（"GenuineIntel" 等） */
static inline void cpuid_get_vendor(char out12[12]) {
    cpuid_regs_t r = cpuid_leaf(0);
    ((uint32_t*)out12)[0] = r.ebx;
    ((uint32_t*)out12)[1] = r.edx;
    ((uint32_t*)out12)[2] = r.ecx;
}

/* CPUID(1).ECX の VMX bit(5) を返す */
static inline int cpuid_has_vmx(void) {
    cpuid_regs_t r = cpuid_leaf(1);
    return (r.ecx & (1u << 5)) != 0;
}
