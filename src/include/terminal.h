#ifndef SPECTREOS_TERMINAL_H
#define SPECTREOS_TERMINAL_H

#include <stdint.h>

void terminal_clear(void);

void terminal_putchar(char c);

void terminal_write(const char* str);

void terminal_set_cursor(uint8_t x, uint8_t y);

uint8_t terminal_get_cursor_x(void);

uint8_t terminal_get_cursor_y(void);

void terminal_put_at(
    uint8_t x,
    uint8_t y,
    char c
);

void terminal_clear_line(void);

void terminal_update_cursor(void);

#endif
