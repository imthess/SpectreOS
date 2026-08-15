#ifndef SPECTREOS_TERMINAL_H
#define SPECTREOS_TERMINAL_H

void terminal_clear(void);
void terminal_putchar(char c);
void terminal_write(const char* str);
void terminal_backspace(void);

#endif