#include <stdint.h>

#include "terminal.h"

#define VGA_MEMORY ((volatile uint16_t*)0xB8000)

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static uint8_t cursor_x = 0;
static uint8_t cursor_y = 0;

void terminal_clear(void)
{
    for (int y = 0; y < VGA_HEIGHT; y++)
    {
        for (int x = 0; x < VGA_WIDTH; x++)
        {
            VGA_MEMORY[
                y * VGA_WIDTH + x
            ] = 0x0700 | ' ';
        }
    }

    cursor_x = 0;
    cursor_y = 0;
}

void terminal_putchar(char c)
{
    if (c == '\n')
    {
        cursor_x = 0;
        cursor_y++;

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
    }

    if (cursor_y >= VGA_HEIGHT)
    {
        cursor_y = 0;
    }
}

void terminal_backspace(void)
{
    if (cursor_x == 0)
    {
        if (cursor_y == 0)
        {
            return;
        }

        cursor_y--;
        cursor_x = VGA_WIDTH - 1;
    }
    else
    {
        cursor_x--;
    }

    VGA_MEMORY[
        cursor_y * VGA_WIDTH + cursor_x
    ] = 0x0700 | ' ';
}

void terminal_write(const char* str)
{
    while (*str)
    {
        terminal_putchar(*str);
        str++;
    }
}
