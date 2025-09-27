#include "pic.h"
#include "arch/x86/arch_x86_io.h"

// ===== ICW definitions =====
typedef union {
    struct __attribute__((packed)) {
        uint8_t icw4       : 1;
        uint8_t single     : 1;
        uint8_t interval4  : 1;
        uint8_t level      : 1;
        uint8_t _icw1      : 1; // must be 1
        uint8_t _unused    : 3;
    } icw1;

    struct __attribute__((packed)) {
        uint8_t offset;
    } icw2;

    struct __attribute__((packed)) {
        uint8_t cascade_id;
    } icw3;

    struct __attribute__((packed)) {
        uint8_t mode_8086   : 1;
        uint8_t auto_eoi    : 1;
        uint8_t buf         : 2;
        uint8_t full_nested : 1;
        uint8_t _reserved   : 3;
    } icw4;

    uint8_t raw;
} Icw;

// ===== OCW definitions =====
typedef union {
    struct __attribute__((packed)) {
        uint8_t imr;
    } ocw1;

    struct __attribute__((packed)) {
        uint8_t level   : 3;
        uint8_t _res    : 2;
        uint8_t eoi     : 1;
        uint8_t sl      : 1;
        uint8_t rotate  : 1;
    } ocw2;

    struct __attribute__((packed)) {
        uint8_t ris      : 1;
        uint8_t read     : 1;
        uint8_t _unused1 : 1;
        uint8_t _reserved1 : 2;
        uint8_t _unused2 : 2;
        uint8_t _reserved2 : 1;
    } ocw3;

    uint8_t raw;
} Ocw;

// ===== IRQ helpers =====
static inline bool irq_is_primary(IrqLine irq) {
    return irq < 8;
}

static inline uint16_t irq_command_port(IrqLine irq) {
    return irq_is_primary(irq) ? PRIMARY_COMMAND_PORT : SECONDARY_COMMAND_PORT;
}

static inline uint16_t irq_data_port(IrqLine irq) {
    return irq_is_primary(irq) ? PRIMARY_DATA_PORT : SECONDARY_DATA_PORT;
}

static inline uint8_t irq_delta(IrqLine irq) {
    return irq_is_primary(irq) ? (uint8_t)irq : (uint8_t)(irq - 8);
}

// ===== PIC control functions =====

static void issue_icw(Icw cw, uint16_t port) {
    outb(port, cw.raw);
    io_wait();
}

static void issue_ocw(Ocw cw, uint16_t port) {
    outb(port, cw.raw);
    io_wait();
}

static void set_imr(uint8_t imr, uint16_t port) {
    Ocw cw;
    cw.ocw1.imr = imr;
    issue_ocw(cw, port);
}

void pic_init(void) {
    cli();

    // ICW1
    Icw icw;
    icw.icw1.icw4 = 1;
    icw.icw1.single = 0;
    icw.icw1.interval4 = 0;
    icw.icw1.level = 0;
    icw.icw1._icw1 = 1;
    icw.icw1._unused = 0;
    issue_icw(icw, PRIMARY_COMMAND_PORT);
    issue_icw(icw, SECONDARY_COMMAND_PORT);

    // ICW2
    icw.icw2.offset = PRIMARY_VECTOR_OFFSET;
    issue_icw(icw, PRIMARY_DATA_PORT);
    icw.icw2.offset = SECONDARY_VECTOR_OFFSET;
    issue_icw(icw, SECONDARY_DATA_PORT);

    // ICW3
    icw.icw3.cascade_id = 0x04; // IRQ2
    issue_icw(icw, PRIMARY_DATA_PORT);
    icw.icw3.cascade_id = 0x02;
    issue_icw(icw, SECONDARY_DATA_PORT);

    // ICW4
    icw.icw4.mode_8086 = 1;
    icw.icw4.auto_eoi = 0;
    icw.icw4.buf = 0;
    icw.icw4.full_nested = 0;
    icw.icw4._reserved = 0;
    issue_icw(icw, PRIMARY_DATA_PORT);
    issue_icw(icw, SECONDARY_DATA_PORT);

    // Mask all IRQs
    set_imr(0xFF, PRIMARY_DATA_PORT);
    set_imr(0xFF, SECONDARY_DATA_PORT);

    sti();
}

void pic_set_mask(IrqLine irq) {
    uint16_t port = irq_data_port(irq);
    uint8_t mask = inb(port);
    mask |= (1 << irq_delta(irq));
    set_imr(mask, port);
}

void pic_unset_mask(IrqLine irq) {
    uint16_t port = irq_data_port(irq);
    uint8_t mask = inb(port);
    mask &= ~(1 << irq_delta(irq));
    set_imr(mask, port);
}

void pic_notify_eoi(IrqLine irq) {
    Ocw cw;
    cw.ocw2.level = irq_delta(irq);
    cw.ocw2._res = 0;
    cw.ocw2.eoi = 1;
    cw.ocw2.sl = 1;
    cw.ocw2.rotate = 0;

    issue_ocw(cw, irq_command_port(irq));

    if (!irq_is_primary(irq)) {
        cw.ocw2.level = 2;
        issue_ocw(cw, PRIMARY_COMMAND_PORT);
    }
}

uint16_t pic_get_irq_mask(void) {
    uint8_t val1 = inb(PRIMARY_DATA_PORT);
    uint8_t val2 = inb(SECONDARY_DATA_PORT);
    return (val2 << 8) | val1;
}

void pic_set_irq_mask(uint16_t mask) {
    set_imr((uint8_t)mask, PRIMARY_DATA_PORT);
    set_imr((uint8_t)(mask >> 8), SECONDARY_DATA_PORT);
}
