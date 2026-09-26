#ifndef SPECTREOS_AUTH_H
#define SPECTREOS_AUTH_H

#include <stdint.h>

/*
 * ============================================================
 * AUTHENTICATION
 *
 * A username/password login gate in front of the shell. On a
 * blank disk (no account database yet) this runs a first-run
 * setup wizard instead of a login prompt: the account created
 * there also becomes the machine's cryptographic identity (see
 * identity.h) - the two are deliberately created together.
 *
 * Passwords are never stored. What's persisted is a random salt
 * plus PBKDF2-HMAC-SHA256(password, salt, iterations, 32) - see
 * pbkdf2.c. The password itself is wiped from memory (see
 * secure_wipe.h) as soon as it has been hashed or compared.
 *
 * Integration note: this project's keyboard handling is
 * callback-driven (keyboard.c calls straight into
 * shell_keyboard_*() or nano_keyboard_*() depending on which is
 * active - see keyboard.c). auth_keyboard_*() slots into that
 * same pattern as a third mode, gated by auth_login_active():
 * keyboard.c must check auth_login_active() before nano/shell
 * and route to auth's handlers instead while it's true.
 * ============================================================
 */

#define AUTH_MAX_USERS       4
#define AUTH_USERNAME_MAX    32
#define AUTH_PASSWORD_MAX    64
#define AUTH_SALT_SIZE       16
#define AUTH_HASH_SIZE       32
#define AUTH_PBKDF2_ITERATIONS 4096

/*
 * Loads the account database from disk if present. Always
 * safe to call at boot even on a blank disk (leaves an empty,
 * in-memory database ready for first-run setup).
 */
void auth_init(void);

/*
 * Starts the login gate: shows the first-run setup wizard if
 * no accounts exist yet, otherwise a login prompt. Does not
 * return until the corresponding keyboard callbacks drive it
 * to completion and it calls shell_init() itself.
 */
void auth_start(void);

/*
 * 1 while the login/setup flow is in control of keyboard input
 * and the shell/nano should not receive it, 0 once a session is
 * active.
 */
int auth_login_active(void);

void auth_keyboard_character(char c);
void auth_keyboard_backspace(void);
void auth_keyboard_enter(void);

/*
 * Ends the current session and returns to the login prompt.
 * Exposed for a shell "logout" command.
 */
void auth_logout(void);

int auth_is_logged_in(void);

const char* auth_current_username(void);

#endif
