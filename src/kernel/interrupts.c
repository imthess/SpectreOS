#include <stdint.h>

#include "keyboard.h"
#include "interrupts.h"
#include "pic.h"

static volatile uint64_t timer_ticks = 0;

void exception_handler(registers_t* regs)
{
    /*
     * For now, any CPU exception is fatal.
     */

    (void)regs;

    __asm__ volatile ("cli");

    volatile uint16_t* video =
        (volatile uint16_t*)0xB8000;

    const char* message =
        "KERNEL EXCEPTION";

    for (uint16_t i = 0; message[i]; i++)
    {
        video[i] =
            0x4F00 | (uint8_t)message[i];
    }

    while (1)
    {
        __asm__ volatile ("hlt");
    }
}

void irq_handler(registers_t* regs)
{
    uint32_t irq =
        regs->int_no - 32;

    switch (irq)
    {
        case 0:
            timer_ticks++;
            break;
            
        case 1:
        keyboard_handler();
        break;

        default:
            break;
    }

    pic_send_eoi(
        (uint8_t)irq
    );
}
