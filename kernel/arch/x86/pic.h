#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "arch/x86/arch_x86_io.h"

/// PIC vector offsets
#define PRIMARY_VECTOR_OFFSET   32
#define SECONDARY_VECTOR_OFFSET (PRIMARY_VECTOR_OFFSET + 8)

/// Ports
#define PRIMARY_COMMAND_PORT    0x20
#define PRIMARY_DATA_PORT       0x21
#define SECONDARY_COMMAND_PORT  0xA0
#define SECONDARY_DATA_PORT     0xA1

/// PS/2 Ports
#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT PS2_STATUS_PORT

/// IRQ line identifiers
typedef enum {
    IRQ_TIMER        = 0,
    IRQ_KEYBOARD     = 1,
    IRQ_SECONDARY    = 2,
    IRQ_SERIAL2      = 3,
    IRQ_SERIAL1      = 4,
    IRQ_PARALLEL23   = 5,
    IRQ_FLOPPY       = 6,
    IRQ_PARALLEL1    = 7,
    IRQ_RTC          = 8,
    IRQ_ACPI         = 9,
    IRQ_OPEN1        = 10,
    IRQ_OPEN2        = 11,
    IRQ_MOUSE        = 12,
    IRQ_COP          = 13,
    IRQ_PRIMARY_ATA  = 14,
    IRQ_SECONDARY_ATA= 15
} IrqLine;

/// Initialize the PIC (remap vectors and mask all IRQs)
void pic_init(void);

/// Mask a specific IRQ line
void pic_set_mask(IrqLine irq);

/// Unmask a specific IRQ line
void pic_unset_mask(IrqLine irq);

/// Notify End of Interrupt (EOI)
void pic_notify_eoi(IrqLine irq);

/// Get current IRQ mask (16 bits)
uint16_t pic_get_irq_mask(void);

/// Set IRQ mask (16 bits)
void pic_set_irq_mask(uint16_t mask);

