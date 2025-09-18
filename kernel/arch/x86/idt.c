#include "idt.h"
#include <string.h>

static __attribute__((aligned(4096))) idt_gate_t g_idt[IDT_MAX_GATES];
static idtr_t g_idtr;

static inline void lidt(const idtr_t* idtr) { __asm__ __volatile__("lidt (%0)"::"r"(idtr)); }

void idt_set_gate(int vec, void (*isr)(void), uint16_t cs_selector, uint8_t gate_type, uint8_t dpl)
{
    uint64_t off = (uint64_t)(uintptr_t)isr;
    idt_gate_t g = {0};
    g.offset_low  = (uint16_t)(off & 0xFFFFu);
    g.selector    = cs_selector;
    g.ist         = 0;
    g.rsv1        = 0;
    g.type        = gate_type & 0xF;   /* 0xE: interrupt gate */
    g.zero        = 0;
    g.dpl         = dpl & 0x3;
    g.p           = 1;
    g.offset_mid  = (uint16_t)((off >> 16) & 0xFFFFu);
    g.offset_high = (uint32_t)((off >> 32) & 0xFFFFFFFFu);
    g.rsv2        = 0;

    g_idt[vec] = g;
}

void idt_init(void)
{
    /* クリアしてから LIDT */
    memset(g_idt, 0, sizeof(g_idt));
    g_idtr.limit = (uint16_t)(sizeof(g_idt) - 1);
    g_idtr.base  = (uint64_t)(uintptr_t)g_idt;
    lidt(&g_idtr);
}
