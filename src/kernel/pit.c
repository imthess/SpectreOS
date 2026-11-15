#include <stdint.h>

#include "pit.h"

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40

#define PIT_BASE_FREQUENCY 1193182

static volatile uint32_t pit_ticks = 0;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

void pit_init(uint32_t frequency)
{
    if (frequency == 0)
    {
        return;
    }

    uint32_t divisor =
        PIT_BASE_FREQUENCY / frequency;

    if (divisor > 65535)
    {
        divisor = 65535;
    }

    if (divisor < 1)
    {
        divisor = 1;
    }

    /*
     * Channel 0
     * Access mode: low byte then high byte
     * Mode 3: square-wave generator
     */
    outb(
        PIT_COMMAND,
        0x36
    );

    outb(
        PIT_CHANNEL0,
        (uint8_t)(divisor & 0xFF)
    );

    outb(
        PIT_CHANNEL0,
        (uint8_t)((divisor >> 8) & 0xFF)
    );
}

uint32_t pit_get_ticks(void)
{
    return pit_ticks;
}

void pit_tick(void)
{
    pit_ticks++;
}