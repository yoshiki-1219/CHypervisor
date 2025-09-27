#include "idt.h"
#include <string.h>

/* IDT のエントリ数 (x86-64 固定) */
#define IDT_MAX_ENTRIES 256

/* IDT 本体と IDTR */
static __attribute__((aligned(4096))) IdtGate g_idt[IDT_MAX_ENTRIES];
static IdtRegister g_idtr;

/* IDT エントリ設定 */
void idt_set_entry(int vector, void (*isr)(void),
                   uint16_t cs_selector, uint8_t gate_type, uint8_t dpl)
{
    uint64_t offset = (uint64_t)(uintptr_t)isr;

    IdtGate gate = {0};
    gate.offset_low   = (uint16_t)(offset & 0xFFFFu);
    gate.selector     = cs_selector;
    gate.ist          = 0;
    gate.reserved1    = 0;
    gate.type         = gate_type & 0xF;   /* 0xE: interrupt gate */
    gate.zero         = 0;
    gate.dpl          = dpl & 0x3;
    gate.present      = 1;
    gate.offset_mid   = (uint16_t)((offset >> 16) & 0xFFFFu);
    gate.offset_high  = (uint32_t)((offset >> 32) & 0xFFFFFFFFu);
    gate.reserved2    = 0;

    g_idt[vector] = gate;
}

/* IDT 初期化 */
void idt_init(void)
{
    /* IDT 全クリア */
    memset(g_idt, 0, sizeof(g_idt));

    g_idtr.limit = (uint16_t)(sizeof(g_idt) - 1);
    g_idtr.base  = (uint64_t)(uintptr_t)g_idt;

    load_idtr(&g_idtr);
}
