#include <stdint.h>

#include "terminal.h"

#define VGA_MEMORY ((volatile uint16_t*)0xB8000)

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

#define VGA_COMMAND 0x3D4
#define VGA_DATA    0x3D5

static uint8_t cursor_x = 0;
static uint8_t cursor_y = 0;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static void terminal_scroll(void)
{
    for (uint8_t y = 1; y < VGA_HEIGHT; y++)
    {
        for (uint8_t x = 0; x < VGA_WIDTH; x++)
        {
            VGA_MEMORY[
                (y - 1) * VGA_WIDTH + x
            ] =
                VGA_MEMORY[
                    y * VGA_WIDTH + x
                ];
        }
    }

    for (uint8_t x = 0; x < VGA_WIDTH; x++)
    {
        VGA_MEMORY[
            (VGA_HEIGHT - 1) * VGA_WIDTH + x
        ] = 0x0700 | ' ';
    }

    cursor_y = VGA_HEIGHT - 1;
    cursor_x = 0;
}

void terminal_update_cursor(void)
{
    uint16_t position =
        cursor_y * VGA_WIDTH + cursor_x;

    /*
     * VGA hardware cursor.
     *
     * We use a narrow cursor shape so it appears
     * as a visible vertical line.
     */
    outb(VGA_COMMAND, 0x0A);
    outb(VGA_DATA, 14);

    outb(VGA_COMMAND, 0x0B);
    outb(VGA_DATA, 15);

    outb(
        VGA_COMMAND,
        0x0F
    );

    outb(
        VGA_DATA,
        (uint8_t)(position & 0xFF)
    );

    outb(
        VGA_COMMAND,
        0x0E
    );

    outb(
        VGA_DATA,
        (uint8_t)((position >> 8) & 0xFF)
    );
}

void terminal_clear(void)
{
    for (uint8_t y = 0; y < VGA_HEIGHT; y++)
    {
        for (uint8_t x = 0; x < VGA_WIDTH; x++)
        {
            VGA_MEMORY[
                y * VGA_WIDTH + x
            ] = 0x0700 | ' ';
        }
    }

    cursor_x = 0;
    cursor_y = 0;

    terminal_update_cursor();
}

void terminal_set_cursor(uint8_t x, uint8_t y)
{
    cursor_x = x;
    cursor_y = y;

    if (cursor_x >= VGA_WIDTH)
    {
        cursor_x = VGA_WIDTH - 1;
    }

    if (cursor_y >= VGA_HEIGHT)
    {
        cursor_y = VGA_HEIGHT - 1;
    }

    terminal_update_cursor();
}

uint8_t terminal_get_cursor_x(void)
{
    return cursor_x;
}

uint8_t terminal_get_cursor_y(void)
{
    return cursor_y;
}

void terminal_put_at(
    uint8_t x,
    uint8_t y,
    char c
)
{
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT)
    {
        return;
    }

    VGA_MEMORY[
        y * VGA_WIDTH + x
    ] = 0x0700 | (uint8_t)c;
}

void terminal_clear_line(void)
{
    for (uint8_t x = 0; x < VGA_WIDTH; x++)
    {
        VGA_MEMORY[
            cursor_y * VGA_WIDTH + x
        ] = 0x0700 | ' ';
    }

    cursor_x = 0;

    terminal_update_cursor();
}

void terminal_putchar(char c)
{
    if (c == '\n')
    {
        cursor_x = 0;
        cursor_y++;

        if (cursor_y >= VGA_HEIGHT)
        {
            terminal_scroll();
        }

        terminal_update_cursor();

        return;
    }

    if (c == '\r')
    {
        cursor_x = 0;

        terminal_update_cursor();

        return;
    }

    if (c == '\b')
    {
        if (cursor_x > 0)
        {
            cursor_x--;

            VGA_MEMORY[
                cursor_y * VGA_WIDTH + cursor_x
            ] = 0x0700 | ' ';
        }

        terminal_update_cursor();

        return;
    }

    VGA_MEMORY[
        cursor_y * VGA_WIDTH + cursor_x
    ] = 0x0700 | (uint8_t)c;

    cursor_x++;

    if (cursor_x >= VGA_WIDTH)
    {
        cursor_x = 0;
        cursor_y++;

        if (cursor_y >= VGA_HEIGHT)
        {
            terminal_scroll();
        }
    }

    terminal_update_cursor();
}

void terminal_write(const char* str)
{
    while (*str)
    {
        terminal_putchar(*str);
        str++;
    }
}
