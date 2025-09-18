#include "isr.h"
#include "gdt.h"      /* sel_gdt(), KERNEL_CS_IDX */
#include "../../log.h"   /* KLOG_* */
#include "idt.h"
#include <stddef.h>

static intr_handler_t g_handlers[256];

static const char* exception_name(unsigned v)
{
    switch (v) {
    case 0:  return "#DE: Divide Error";
    case 1:  return "#DB: Debug";
    case 2:  return "NMI";
    case 3:  return "#BP: Breakpoint";
    case 4:  return "#OF: Overflow";
    case 5:  return "#BR: BOUND Range Exceeded";
    case 6:  return "#UD: Invalid Opcode";
    case 7:  return "#NM: Device Not Available";
    case 8:  return "#DF: Double Fault";
    case 10: return "#TS: Invalid TSS";
    case 11: return "#NP: Segment Not Present";
    case 12: return "#SS: Stack-Segment Fault";
    case 13: return "#GP: General Protection";
    case 14: return "#PF: Page Fault";
    case 16: return "#MF: x87 FP Error";
    case 17: return "#AC: Alignment Check";
    case 18: return "#MC: Machine Check";
    case 19: return "#XM: SIMD FP Exception";
    case 20: return "#VE: Virtualization Exception";
    case 21: return "#CP: Control Protection";
    default: return "External/Software Interrupt";
    }
}

static void default_unhandled(intr_context_t* c)
{
    KLOG_ERROR("intr", "============ Oops! ===================");
    KLOG_ERROR("intr", "Unhandled interrupt: %s (%u)", exception_name((unsigned)c->vector), (unsigned)c->vector);
    KLOG_ERROR("intr", "Error Code: 0x%llx", (unsigned long long)c->error_code);
    KLOG_ERROR("intr", "RIP    : 0x%016llx", (unsigned long long)c->rip);
    KLOG_ERROR("intr", "RFLAGS : 0x%016llx", (unsigned long long)c->rflags);
    KLOG_ERROR("intr", "RAX    : 0x%016llx", (unsigned long long)c->regs.rax);
    KLOG_ERROR("intr", "RBX    : 0x%016llx", (unsigned long long)c->regs.rbx);
    KLOG_ERROR("intr", "RCX    : 0x%016llx", (unsigned long long)c->regs.rcx);
    KLOG_ERROR("intr", "RDX    : 0x%016llx", (unsigned long long)c->regs.rdx);
    KLOG_ERROR("intr", "RSI    : 0x%016llx", (unsigned long long)c->regs.rsi);
    KLOG_ERROR("intr", "RDI    : 0x%016llx", (unsigned long long)c->regs.rdi);
    KLOG_ERROR("intr", "RSP    : 0x%016llx", (unsigned long long)c->regs.rsp);
    KLOG_ERROR("intr", "RBP    : 0x%016llx", (unsigned long long)c->regs.rbp);
    KLOG_ERROR("intr", "R8     : 0x%016llx", (unsigned long long)c->regs.r8);
    KLOG_ERROR("intr", "R9     : 0x%016llx", (unsigned long long)c->regs.r9);
    KLOG_ERROR("intr", "R10    : 0x%016llx", (unsigned long long)c->regs.r10);
    KLOG_ERROR("intr", "R11    : 0x%016llx", (unsigned long long)c->regs.r11);
    KLOG_ERROR("intr", "R12    : 0x%016llx", (unsigned long long)c->regs.r12);
    KLOG_ERROR("intr", "R13    : 0x%016llx", (unsigned long long)c->regs.r13);
    KLOG_ERROR("intr", "R14    : 0x%016llx", (unsigned long long)c->regs.r14);
    KLOG_ERROR("intr", "R15    : 0x%016llx", (unsigned long long)c->regs.r15);
    KLOG_ERROR("intr", "CS     : 0x%04llx", (unsigned long long)c->cs);

    for(;;) __asm__ __volatile__("hlt");
}

void intr_register_handler(int vec, intr_handler_t fn)
{
    if (vec < 0 || vec >= 256) return;
    g_handlers[vec] = fn ? fn : default_unhandled;
}

/* 共通 ISR から C 呼び出し時の入口 */
void intr_dispatch_entry(intr_context_t* ctx)
{
    intr_handler_t h = g_handlers[(unsigned)ctx->vector];
    h(ctx);
}

/* 256 本のスタブを IDT に登録し、IF=1 */
extern void (*__isr_stub_table[])(void); /* isr_stubs.c で定義 */
void intr_init_all_vectors(void)
{
    /* まずすべてデフォルトに */
    for (int i = 0; i < 256; ++i) g_handlers[i] = default_unhandled;

    /* IDT 生成・ロード */
    idt_init();

    /* CS セレクタ（前回の GDT 実装で index=KERNEL_CS_IDX を使用） */
    uint16_t cs = sel_gdt(KERNEL_CS_IDX, 0);

    for (int v = 0; v < 256; ++v) {
        idt_set_gate(v, __isr_stub_table[v], cs, 0xE /*interrupt*/, 0 /*ring0*/);
    }

    /* 割込み有効化 */
    __asm__ __volatile__("sti");
}
