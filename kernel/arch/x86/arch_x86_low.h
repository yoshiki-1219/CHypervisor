#pragma once
#include <stdint.h>

/* ---- MSR アクセス ---- */
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val)
{
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ __volatile__("wrmsr" :: "c"(msr), "a"(lo), "d"(hi));
}

/* セグメントセレクタ */
static inline uint16_t read_cs(void){ uint16_t v; __asm__ __volatile__("mov %%cs,%0":"=r"(v)); return v; }
static inline uint16_t read_ss(void){ uint16_t v; __asm__ __volatile__("mov %%ss,%0":"=r"(v)); return v; }
static inline uint16_t read_ds(void){ uint16_t v; __asm__ __volatile__("mov %%ds,%0":"=r"(v)); return v; }
static inline uint16_t read_es(void){ uint16_t v; __asm__ __volatile__("mov %%es,%0":"=r"(v)); return v; }
static inline uint16_t read_fs(void){ uint16_t v; __asm__ __volatile__("mov %%fs,%0":"=r"(v)); return v; }
static inline uint16_t read_gs(void){ uint16_t v; __asm__ __volatile__("mov %%gs,%0":"=r"(v)); return v; }
static inline uint16_t read_tr(void){ uint16_t v; __asm__ __volatile__("str %0":"=r"(v)); return v; }

/* FS/GS ベースは MSR */
#define IA32_FS_BASE 0xC0000100
#define IA32_GS_BASE 0xC0000101
#define IA32_EFER    0xC0000080

/* SGDT/SIDT */
struct desc_ptr { uint16_t limit; uint64_t base; } __attribute__((packed));
static inline struct desc_ptr sgdt_get(void){struct desc_ptr g;__asm__ __volatile__("sgdt %0":"=m"(g));return g;}
static inline struct desc_ptr sidt_get(void){struct desc_ptr i;__asm__ __volatile__("sidt %0":"=m"(i));return i;}

/* CR0/CR3/CR4 */
static inline uint64_t read_cr0(void){uint64_t v;__asm__ __volatile__("mov %%cr0,%0":"=r"(v));return v;}
static inline uint64_t read_cr3(void){uint64_t v;__asm__ __volatile__("mov %%cr3,%0":"=r"(v));return v;}
static inline uint64_t read_cr4(void){uint64_t v;__asm__ __volatile__("mov %%cr4,%0":"=r"(v));return v;}
static inline void write_cr0(uint64_t v){__asm__ __volatile__("mov %0,%%cr0"::"r"(v):"memory"); }
static inline void write_cr3(uint64_t v){__asm__ __volatile__("mov %0,%%cr3"::"r"(v):"memory");}
static inline void write_cr4(uint64_t v){__asm__ __volatile__("mov %0,%%cr4"::"r"(v):"memory");}
