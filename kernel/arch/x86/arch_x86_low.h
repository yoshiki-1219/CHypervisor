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

/* ==========================================================
 * CR helpers (mask-based, no C bitfields)
 * ========================================================== */

/* CR0 bit positions (SDM Vol.3) */
#define CR0_PE  (1ull << 0)   /* Protected Mode Enable */
#define CR0_MP  (1ull << 1)   /* Monitor Coprocessor   */
#define CR0_EM  (1ull << 2)   /* Emulation             */
#define CR0_TS  (1ull << 3)   /* Task Switched         */
#define CR0_ET  (1ull << 4)   /* Extension Type        */
#define CR0_NE  (1ull << 5)   /* Numeric Error         */
/* [15:6] reserved in Zig view ⇒ ここはRMWで保持 */
#define CR0_WP  (1ull << 16)  /* Write Protect         */
/* [17] reserved */
#define CR0_AM  (1ull << 18)  /* Alignment Mask        */
/* [28:19] reserved */
#define CR0_NW  (1ull << 29)  /* Not Write-through     */
#define CR0_CD  (1ull << 30)  /* Cache Disable         */
#define CR0_PG  (1ull << 31)  /* Paging                */
/* [63:32] reserved */

/* CR4 bit positions (SDM Vol.3) */
#define CR4_VME        (1ull << 0)   /* Virtual-8086 Mode Extensions */
#define CR4_PVI        (1ull << 1)   /* Protected Mode Virtual Interrupts */
#define CR4_TSD        (1ull << 2)   /* Time Stamp Disable */
#define CR4_DE         (1ull << 3)   /* Debugging Extensions */
#define CR4_PSE        (1ull << 4)   /* Page Size Extensions */
#define CR4_PAE        (1ull << 5)   /* Physical Address Extension */
#define CR4_MCE        (1ull << 6)   /* Machine Check Enable */
#define CR4_PGE        (1ull << 7)   /* Page Global Enable */
#define CR4_PCE        (1ull << 8)   /* Performance Monitoring Counter Enable */
#define CR4_OSFXSR     (1ull << 9)   /* OS support for FXSAVE/FXRSTOR */
#define CR4_OSXMMEXCPT (1ull << 10)  /* OS support for unmasked SIMD FP exceptions */
#define CR4_UMIP       (1ull << 11)  /* User-Mode Instruction Prevention */
#define CR4_LA57       (1ull << 12)  /* 5-Level Paging (57-bit linear addresses) */
#define CR4_VMXE       (1ull << 13)  /* VMX Enable */
#define CR4_SMXE       (1ull << 14)  /* SMX Enable */
/* [15] reserved */
#define CR4_FSGSBASE   (1ull << 16)  /* Enable RDFSBASE/WRFSBASE/etc. */
#define CR4_PCIDE      (1ull << 17)  /* PCID Enable */
#define CR4_OSXSAVE    (1ull << 18)  /* XSAVE and extended states enable */
/* [19] reserved */
#define CR4_SMEP       (1ull << 20)  /* Supervisor Mode Execution Prevention */
#define CR4_SMAP       (1ull << 21)  /* Supervisor Mode Access Prevention */
#define CR4_PKE        (1ull << 22)  /* Protection Key Enable */
#define CR4_CET        (1ull << 23)  /* Control-flow Enforcement Technology */
#define CR4_PKS        (1ull << 24)  /* Protection Keys for Supervisor pages */
/* [63:25] reserved */

