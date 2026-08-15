#include <stdint.h>

#include "keyboard.h"
#include "terminal.h"

/*
 * PS/2 keyboard controller ports.
 */
#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

/*
 * US keyboard scancode set 1.
 */
static const char keyboard_map[128] =
{
    0,   27,  '1', '2', '3', '4', '5', '6',
    '7', '8', '9', '0', '-', '=', '\b', '\t',

    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\n', 0,   'a', 's',

    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',

    'b', 'n', 'm', ',', '.', '/', 0,   '*',
    0,   ' ', 0,   0,   0,   0,   0,   0,

    0,   0,   0,   0,   0,   0,   0,   '7',
    '8', '9', '-', '4', '5', '6', '+', '1',

    '2', '3', '0', '.'
};

/*
 * Shifted keyboard characters.
 *
 * This table contains the characters produced
 * when Shift is held.
 */
static const char keyboard_shift_map[128] =
{
    0,   27,  '!', '@', '#', '$', '%', '^',
    '&', '*', '(', ')', '_', '+', '\b', '\t',

    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\n', 0,   'A', 'S',

    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0,   '|', 'Z', 'X', 'C', 'V',

    'B', 'N', 'M', '<', '>', '?', 0,   '*',
    0,   ' ', 0,   0,   0,   0,   0,   0,

    0,   0,   0,   0,   0,   0,   0,   '7',
    '8', '9', '-', '4', '5', '6', '+', '1',

    '2', '3', '0', '.'
};

/*
 * Keyboard modifier state.
 */
static uint8_t shift_pressed = 0;
static uint8_t caps_lock = 0;

void keyboard_init(void)
{
    /*
     * Read the keyboard status port once.
     *
     * IRQ configuration is handled by the PIC.
     */
    (void)inb(KEYBOARD_STATUS_PORT);
}

void keyboard_handler(void)
{
    uint8_t scancode =
        inb(KEYBOARD_DATA_PORT);

    /*
     * Left Shift pressed.
     */
    if (scancode == 0x2A)
    {
        shift_pressed = 1;
        return;
    }

    /*
     * Right Shift pressed.
     */
    if (scancode == 0x36)
    {
        shift_pressed = 1;
        return;
    }

    /*
     * Left Shift released.
     */
    if (scancode == 0xAA)
    {
        shift_pressed = 0;
        return;
    }

    /*
     * Right Shift released.
     */
    if (scancode == 0xB6)
    {
        shift_pressed = 0;
        return;
    }

    /*
     * Caps Lock.
     *
     * Scancode 0x3A means the key was pressed.
     */
    if (scancode == 0x3A)
    {
        caps_lock ^= 1;
        return;
    }

    /*
     * Ignore all other key-release codes.
     */
    if (scancode & 0x80)
    {
        return;
    }

    /*
     * Backspace.
     */
    if (scancode == 0x0E)
    {
        terminal_backspace();
        return;
    }

    /*
     * Ignore invalid scancodes.
     */
    if (scancode >= 128)
    {
        return;
    }

    char character;

    /*
     * Select the normal or shifted character.
     */
    if (shift_pressed)
    {
        character =
            keyboard_shift_map[scancode];
    }
    else
    {
        character =
            keyboard_map[scancode];
    }

    /*
     * Caps Lock affects letters only.
     *
     * This gives the normal behavior:
     *
     * Caps Lock + A -> A
     * Caps Lock + Shift + A -> a
     */
    if (caps_lock)
    {
        if (character >= 'a' &&
            character <= 'z')
        {
            character =
                character - 'a' + 'A';
        }
        else if (character >= 'A' &&
                 character <= 'Z')
        {
            character =
                character - 'A' + 'a';
        }
    }

    /*
     * Print the character.
     */
    if (character != 0)
    {
        terminal_putchar(character);
    }
}