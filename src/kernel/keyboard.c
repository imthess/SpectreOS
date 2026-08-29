#include <stdint.h>

#include "keyboard.h"
#include "shell.h"
#include "nano.h"

#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64

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

static volatile int left_shift = 0;
static volatile int right_shift = 0;
static volatile int caps_lock = 0;
static volatile int ctrl_pressed = 0;
static volatile int alt_pressed = 0;
static volatile int extended = 0;

static int shift_active(void)
{
    return left_shift || right_shift;
}

static int is_letter(char c)
{
    return c >= 'a' && c <= 'z';
}

static char apply_case(
    uint8_t scancode
)
{
    char normal = keyboard_map[scancode];

    if (!is_letter(normal))
    {
        return shift_active()
            ? keyboard_shift_map[scancode]
            : normal;
    }

    /*
     * XOR behavior:
     *
     * neither     -> lowercase
     * shift       -> uppercase
     * caps        -> uppercase
     * shift+caps  -> lowercase
     */
    if (shift_active() ^ caps_lock)
    {
        return (char)(normal - 'a' + 'A');
    }

    return normal;
}

void keyboard_init(void)
{
    left_shift = 0;
    right_shift = 0;
    caps_lock = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    extended = 0;

    /*
     * Do not consume keyboard data here.
     * Only read controller status.
     */
    (void)inb(KEYBOARD_STATUS_PORT);
}

/*
 * Dispatch helpers: route to nano while it owns the keyboard,
 * otherwise route to the shell. Keeping this as small wrapper
 * functions means the scancode-handling logic below never has
 * to branch on "which mode are we in" more than once per key.
 */

static void dispatch_character(char c)
{
    if (nano_active())
    {
        nano_keyboard_character(c);
    }
    else
    {
        shell_keyboard_character(c);
    }
}

static void dispatch_enter(void)
{
    if (nano_active())
    {
        nano_keyboard_enter();
    }
    else
    {
        shell_keyboard_enter();
    }
}

static void dispatch_backspace(void)
{
    if (nano_active())
    {
        nano_keyboard_backspace();
    }
    else
    {
        shell_keyboard_backspace();
    }
}

static void dispatch_delete(void)
{
    if (nano_active())
    {
        nano_keyboard_delete();
    }
    else
    {
        shell_keyboard_delete();
    }
}

static void dispatch_left(void)
{
    if (nano_active())
    {
        nano_keyboard_left();
    }
    else
    {
        shell_keyboard_left();
    }
}

static void dispatch_right(void)
{
    if (nano_active())
    {
        nano_keyboard_right();
    }
    else
    {
        shell_keyboard_right();
    }
}

static void dispatch_up(void)
{
    if (nano_active())
    {
        nano_keyboard_up();
    }
    else
    {
        shell_keyboard_up();
    }
}

static void dispatch_down(void)
{
    if (nano_active())
    {
        nano_keyboard_down();
    }
    else
    {
        shell_keyboard_down();
    }
}

static void dispatch_home(void)
{
    if (nano_active())
    {
        nano_keyboard_home();
    }
    else
    {
        shell_keyboard_home();
    }
}

static void dispatch_end(void)
{
    if (nano_active())
    {
        nano_keyboard_end();
    }
    else
    {
        shell_keyboard_end();
    }
}

void keyboard_handler(void)
{
    uint8_t scancode =
        inb(KEYBOARD_DATA_PORT);

    /*
     * Extended scancode prefix.
     */
    if (scancode == 0xE0)
    {
        extended = 1;
        return;
    }

    /*
     * Extended key.
     */
    if (extended)
    {
        extended = 0;

        /*
         * Right Ctrl.
         */
        if (scancode == 0x1D)
        {
            ctrl_pressed = 1;
            return;
        }

        /*
         * Right Ctrl release.
         */
        if (scancode == 0x9D)
        {
            ctrl_pressed = 0;
            return;
        }

        /*
         * Extended key release.
         */
        if (scancode & 0x80)
        {
            return;
        }

        switch (scancode)
        {
            case 0x4B:
                dispatch_left();
                return;

            case 0x4D:
                dispatch_right();
                return;

            case 0x48:
                dispatch_up();
                return;

            case 0x50:
                dispatch_down();
                return;

            case 0x53:
                dispatch_delete();
                return;

            case 0x47:
                dispatch_home();
                return;

            case 0x4F:
                dispatch_end();
                return;

            default:
                return;
        }
    }

    /*
     * Left Shift.
     */
    if (scancode == 0x2A)
    {
        left_shift = 1;
        return;
    }

    if (scancode == 0xAA)
    {
        left_shift = 0;
        return;
    }

    /*
     * Right Shift.
     */
    if (scancode == 0x36)
    {
        right_shift = 1;
        return;
    }

    if (scancode == 0xB6)
    {
        right_shift = 0;
        return;
    }

    /*
     * Left Ctrl.
     */
    if (scancode == 0x1D)
    {
        ctrl_pressed = 1;
        return;
    }

    if (scancode == 0x9D)
    {
        ctrl_pressed = 0;
        return;
    }

    /*
     * Left Alt.
     */
    if (scancode == 0x38)
    {
        alt_pressed = 1;
        return;
    }

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
     * Ignore key release.
     */
    if (scancode & 0x80)
    {
        return;
    }

    if (scancode >= 128)
    {
        return;
    }

    /*
     * Ctrl handling.
     *
     * When Ctrl is held, printable letter keys are reported to
     * whichever mode owns the keyboard (nano or the shell) via
     * its ctrl handler instead of being turned into a normal
     * character. This runs before the Enter/Backspace/Tab
     * checks below so e.g. Ctrl+O is never confused with a
     * plain 'o' keystroke.
     */
    if (ctrl_pressed)
    {
        char base = keyboard_map[scancode];

        if (is_letter(base))
        {
            if (nano_active())
            {
                nano_keyboard_ctrl(base);
            }

            /*
             * The shell has no Ctrl+letter bindings today;
             * Ctrl+C and any other combo are still no-ops
             * outside nano, matching prior behavior.
             */
            return;
        }

        return;
    }

    /*
     * Enter.
     */
    if (scancode == 0x1C)
    {
        dispatch_enter();
        return;
    }

    /*
     * Backspace.
     */
    if (scancode == 0x0E)
    {
        dispatch_backspace();
        return;
    }

    /*
     * Tab.
     */
    if (scancode == 0x0F)
    {
        dispatch_character('\t');
        return;
    }

    /*
     * Alt handling.
     */
    if (alt_pressed)
    {
        return;
    }

    /*
     * Convert printable key.
     */
    char character =
        apply_case(scancode);

    if (character != 0)
    {
        dispatch_character(character);
    }
}
