#ifndef SPECTREOS_NANO_H
#define SPECTREOS_NANO_H

/*
 * ------------------------------------------------------------
 * SpectreOS nano-style full-screen text editor.
 *
 * Usage from the shell:
 *
 *   nano_open("myfile.txt");
 *
 * While nano is active, keyboard.c routes input here instead
 * of to the shell (see nano_active()). Ctrl+O saves (prompts
 * for a filename, defaulting to the one nano was opened with;
 * press Enter to confirm). Ctrl+X exits back to the shell.
 * ------------------------------------------------------------
 */

/*
 * Returns 1 while nano owns the keyboard, 0 otherwise.
 * keyboard.c checks this before routing input to the shell.
 */
int nano_active(void);

/*
 * Opens nano as a full-screen editor. If the named file
 * already exists it is loaded; otherwise nano starts with
 * an empty buffer using that filename as the save target.
 */
void nano_open(const char* filename);

/*
 * Keyboard entry points. Only called while nano_active() is 1.
 */
void nano_keyboard_character(char c);
void nano_keyboard_backspace(void);
void nano_keyboard_delete(void);
void nano_keyboard_left(void);
void nano_keyboard_right(void);
void nano_keyboard_up(void);
void nano_keyboard_down(void);
void nano_keyboard_home(void);
void nano_keyboard_end(void);
void nano_keyboard_enter(void);

/*
 * Called when Ctrl is held and the given (lowercased) letter
 * is pressed, e.g. nano_keyboard_ctrl('o') for Ctrl+O.
 */
void nano_keyboard_ctrl(char letter);

#endif
