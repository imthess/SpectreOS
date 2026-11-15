#ifndef SPECTREOS_TERMINAL_H
#define SPECTREOS_TERMINAL_H

#include <stdint.h>

void terminal_clear(void);

void terminal_putchar(char c);

void terminal_write(const char* str);

void terminal_write_uint(uint32_t value);

void terminal_put_at(
    uint8_t x,
    uint8_t y,
    char c
);

void terminal_set_cursor(
    uint8_t x,
    uint8_t y
);

void terminal_update_cursor(void);

uint8_t terminal_get_cursor_x(void);

uint8_t terminal_get_cursor_y(void);

#endif
