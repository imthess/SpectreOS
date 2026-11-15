#ifndef SPECTREOS_SHELL_H
#define SPECTREOS_SHELL_H

void shell_init(void);

void shell_keyboard_character(char c);
void shell_keyboard_backspace(void);
void shell_keyboard_delete(void);

void shell_keyboard_left(void);
void shell_keyboard_right(void);
void shell_keyboard_home(void);
void shell_keyboard_end(void);

void shell_keyboard_up(void);
void shell_keyboard_down(void);

void shell_keyboard_enter(void);

void shell_resume_prompt(void);

#endif
