#include <stdint.h>

#include "serial.h"

#define COM1_PORT 0x3F8

#define REG_DATA          0
#define REG_INT_ENABLE    1
#define REG_FIFO_CTRL     2
#define REG_LINE_CTRL     3
#define REG_MODEM_CTRL    4
#define REG_LINE_STATUS   5

#define LSR_TX_EMPTY 0x20

static int port_present = 0;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int run_loopback_test(void)
{
    /*
     * Set MCR to loopback mode (bit 4) plus the same OUT1/OUT2
     * bits used in normal operation, so this test exercises the
     * same electrical path normal writes will use.
     */
    outb(COM1_PORT + REG_MODEM_CTRL, 0x1E);

    outb(COM1_PORT + REG_DATA, 0xAE);

    uint8_t echoed =
        inb(COM1_PORT + REG_DATA);

    return echoed == 0xAE;
}

void serial_init(void)
{
    outb(COM1_PORT + REG_INT_ENABLE, 0x00);

    /* Enable DLAB, set divisor for 38400 baud. */
    outb(COM1_PORT + REG_LINE_CTRL, 0x80);
    outb(COM1_PORT + REG_DATA, 0x03);
    outb(COM1_PORT + REG_INT_ENABLE, 0x00);

    /* 8 bits, no parity, one stop bit; DLAB cleared. */
    outb(COM1_PORT + REG_LINE_CTRL, 0x03);

    /* Enable FIFO, clear it, 14-byte trigger threshold. */
    outb(COM1_PORT + REG_FIFO_CTRL, 0xC7);

    if (!run_loopback_test())
    {
        port_present = 0;

        return;
    }

    /* Normal operation: DTR, RTS, OUT1, OUT2. */
    outb(COM1_PORT + REG_MODEM_CTRL, 0x0F);

    port_present = 1;
}

int serial_present(void)
{
    return port_present;
}

static void serial_put_char(char c)
{
    uint32_t waited = 0;

    while (!(inb(COM1_PORT + REG_LINE_STATUS) & LSR_TX_EMPTY))
    {
        waited++;

        if (waited > 100000u)
        {
            return;
        }
    }

    outb(COM1_PORT + REG_DATA, (uint8_t)c);
}

void serial_write(const char* text)
{
    if (!port_present || text == 0)
    {
        return;
    }

    for (uint32_t i = 0; text[i] != '\0'; i++)
    {
        if (text[i] == '\n')
        {
            serial_put_char('\r');
        }

        serial_put_char(text[i]);
    }
}

void serial_putchar(char c)
{
    if (!port_present)
    {
        return;
    }

    if (c == '\n')
    {
        serial_put_char('\r');
    }

    serial_put_char(c);
}
