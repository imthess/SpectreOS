#include <stdint.h>

#include "keyboard.h"
#include "shell.h"

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
 * Set 1 scancode map.
 */
static const char keyboard_map[128] =
{
    0,    27,  '1', '2', '3', '4', '5', '6',
    '7',  '8',  '9', '0', '-', '=', '\b', '\t',

    'q',  'w',  'e', 'r', 't', 'y', 'u', 'i',
    'o',  'p',  '[', ']', '\n', 0,   'a', 's',

    'd',  'f',  'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',

    'b',  'n',  'm', ',', '.', '/', 0,   '*',
    0,    ' ', 0,   0,   0,   0,   0,   0,

    0,    0,   0,   0,   0,   0,   0,   '7',
    '8',  '9',  '-', '4', '5', '6', '+', '1',

    '2',  '3',  '0', '.'
};

/*
 * Shifted set 1 map.
 */
static const char keyboard_shift_map[128] =
{
    0,    27,  '!', '@', '#', '$', '%', '^',
    '&',  '*',  '(', ')', '_', '+', '\b', '\t',

    'Q',  'W',  'E', 'R', 'T', 'Y', 'U', 'I',
    'O',  'P',  '{', '}', '\n', 0,   'A', 'S',

    'D',  'F',  'G', 'H', 'J', 'K', 'L', ':',
    '"',  '~',  0,   '|', 'Z', 'X', 'C', 'V',

    'B',  'N',  'M', '<', '>', '?', 0,   '*',
    0,    ' ', 0,   0,   0,   0,   0,   0,

    0,    0,   0,   0,   0,   0,   0,   '7',
    '8',  '9',  '-', '4', '5', '6', '+', '1',

    '2',  '3',  '0', '.'
};

static int left_shift = 0;
static int right_shift = 0;
static int caps_lock = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;

static int extended_scancode = 0;

static int shift_active(void)
{
    return left_shift || right_shift;
}

static int is_letter(char c)
{
    return (
        c >= 'a' &&
        c <= 'z'
    );
}

void keyboard_init(void)
{
    /*
     * Clear keyboard state.
     */
    left_shift = 0;
    right_shift = 0;
    caps_lock = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    extended_scancode = 0;

    /*
     * Read status port.
     */
    (void)inb(KEYBOARD_STATUS_PORT);
}

void keyboard_handler(void)
{
    uint8_t scancode =
        inb(KEYBOARD_DATA_PORT);

    /*
     * E0 means the next scancode is an
     * extended keyboard key.
     */
    if (scancode == 0xE0)
    {
        extended_scancode = 1;
        return;
    }

    /*
     * Extended key handling.
     */
    if (extended_scancode)
    {
        extended_scancode = 0;

        /*
         * Ignore extended key releases for now.
         */
        if (scancode & 0x80)
        {
            return;
        }

        switch (scancode)
        {
            /*
             * Right arrow.
             */
            case 0x4D:
                shell_keyboard_right();
                return;

            /*
             * Left arrow.
             */
            case 0x4B:
                shell_keyboard_left();
                return;

            /*
             * Up arrow.
             */
            case 0x48:
                shell_keyboard_up();
                return;

            /*
             * Down arrow.
             */
            case 0x50:
                shell_keyboard_down();
                return;

            /*
             * Delete.
             */
            case 0x53:
                shell_keyboard_delete();
                return;

            /*
             * Home.
             */
            case 0x47:
                shell_keyboard_home();
                return;

            /*
             * End.
             */
            case 0x4F:
                shell_keyboard_end();
                return;

            /*
             * Right Ctrl press.
             */
            case 0x1D:
                ctrl_pressed = 1;
                return;

            default:
                return;
        }
    }

    /*
     * Left Shift press.
     */
    if (scancode == 0x2A)
    {
        left_shift = 1;
        return;
    }

    /*
     * Right Shift press.
     */
    if (scancode == 0x36)
    {
        right_shift = 1;
        return;
    }

    /*
     * Left Shift release.
     */
    if (scancode == 0xAA)
    {
        left_shift = 0;
        return;
    }

    /*
     * Right Shift release.
     */
    if (scancode == 0xB6)
    {
        right_shift = 0;
        return;
    }

    /*
     * Left Ctrl press.
     */
    if (scancode == 0x1D)
    {
        ctrl_pressed = 1;
        return;
    }

    /*
     * Left Ctrl release.
     */
    if (scancode == 0x9D)
    {
        ctrl_pressed = 0;
        return;
    }

    /*
     * Left Alt press.
     */
    if (scancode == 0x38)
    {
        alt_pressed = 1;
        return;
    }

    /*
     * Left Alt release.
     */
    if (scancode == 0xB8)
    {
        alt_pressed = 0;
        return;
    }

    /*
     * Caps Lock.
     */
    if (scancode == 0x3A)
    {
        caps_lock = !caps_lock;
        return;
    }

    /*
     * Ignore all ordinary key releases.
     */
    if (scancode & 0x80)
    {
        return;
    }

    if (scancode >= 128)
    {
        return;
    }

    char character;

    /*
     * Select normal or shifted map.
     */
    if (shift_active())
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
     * Alphabetic keys.
     *
     * Caps Lock and Shift have opposite
     * effects on letters.
     */
    if (is_letter(keyboard_map[scancode]))
    {
        if (caps_lock)
        {
            if (shift_active())
            {
                character =
                    keyboard_map[scancode];
            }
            else
            {
                character =
                    keyboard_map[scancode]
                    - 'a' + 'A';
            }
        }
    }

    /*
     * Ctrl combinations.
     */
    if (ctrl_pressed)
    {
        /*
         * Ctrl+C.
         */
        if (character == 'c' ||
            character == 'C')
        {
            return;
        }

        return;
    }

    /*
     * Alt combinations are currently ignored.
     */
    if (alt_pressed)
    {
        return;
    }

    /*
     * Enter.
     */
    if (scancode == 0x1C)
    {
        shell_keyboard_enter();
        return;
    }

    /*
     * Backspace.
     */
    if (scancode == 0x0E)
    {
        shell_keyboard_backspace();
        return;
    }

    /*
     * Tab.
     */
    if (scancode == 0x0F)
    {
        shell_keyboard_character('\t');
        return;
    }

    /*
     * Normal printable character.
     */
    if (character != 0)
    {
        shell_keyboard_character(character);
    }
}
