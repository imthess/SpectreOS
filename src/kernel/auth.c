#include <stdint.h>

#include "auth.h"
#include "identity.h"
#include "pbkdf2.h"
#include "rng.h"
#include "fs.h"
#include "terminal.h"
#include "shell.h"
#include "secure_wipe.h"

#define AUTH_DB_FILE   "users.dat"
#define AUTH_DB_MAGIC  0x53415554u /* "SAUT" */

typedef struct
{
    char username[AUTH_USERNAME_MAX];
    uint8_t salt[AUTH_SALT_SIZE];
    uint8_t hash[AUTH_HASH_SIZE];
    uint32_t in_use;

} auth_record_t;

typedef struct
{
    uint32_t magic;
    uint32_t count;
    auth_record_t users[AUTH_MAX_USERS];

} auth_database_t;

typedef enum
{
    STAGE_INACTIVE,
    STAGE_SIGNUP_USERNAME,
    STAGE_SIGNUP_PASSWORD,
    STAGE_SIGNUP_CONFIRM,
    STAGE_LOGIN_USERNAME,
    STAGE_LOGIN_PASSWORD

} auth_stage_t;

static auth_database_t db;
static int db_loaded = 0;

static auth_stage_t stage = STAGE_INACTIVE;

static char line_buffer[AUTH_PASSWORD_MAX + 1];
static uint32_t line_length = 0;

static char pending_username[AUTH_USERNAME_MAX];
static char pending_password[AUTH_PASSWORD_MAX + 1];

static int logged_in = 0;
static char active_username[AUTH_USERNAME_MAX];

/*
 * ------------------------------------------------------------
 * Small local string helpers (each file in this project keeps
 * its own - see shell.c/identity.c for the same pattern).
 * ------------------------------------------------------------
 */

static void string_copy_bounded(
    char* destination,
    const char* source,
    uint32_t max_length
)
{
    uint32_t i = 0;

    while (source[i] != '\0' &&
           i < (max_length - 1))
    {
        destination[i] = source[i];
        i++;
    }

    destination[i] = '\0';
}

static uint32_t string_length(const char* s)
{
    uint32_t n = 0;

    while (s[n] != '\0')
    {
        n++;
    }

    return n;
}

static int string_equals(
    const char* a,
    const char* b
)
{
    uint32_t i = 0;

    while (a[i] && b[i])
    {
        if (a[i] != b[i])
        {
            return 0;
        }

        i++;
    }

    return (a[i] == '\0' && b[i] == '\0');
}

static int constant_time_equal(
    const uint8_t* a,
    const uint8_t* b,
    uint32_t length
)
{
    uint8_t diff = 0;

    for (uint32_t i = 0; i < length; i++)
    {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }

    return diff == 0;
}

/*
 * ------------------------------------------------------------
 * Account database persistence
 * ------------------------------------------------------------
 */

static void auth_db_save(void)
{
    if (!fs_exists(AUTH_DB_FILE))
    {
        if (!fs_create(AUTH_DB_FILE))
        {
            return;
        }
    }

    int fd = fs_open(AUTH_DB_FILE);

    if (fd < 0)
    {
        return;
    }

    fs_write(fd, &db, sizeof(db));
    fs_close(fd);
}

static void auth_db_load(void)
{
    db.magic = AUTH_DB_MAGIC;
    db.count = 0;

    for (uint32_t i = 0; i < AUTH_MAX_USERS; i++)
    {
        db.users[i].in_use = 0;
    }

    if (fs_exists(AUTH_DB_FILE))
    {
        int fd = fs_open(AUTH_DB_FILE);

        if (fd >= 0)
        {
            auth_database_t loaded_db;

            int read_bytes =
                fs_read(
                    fd,
                    &loaded_db,
                    sizeof(loaded_db)
                );

            fs_close(fd);

            if (read_bytes == (int)sizeof(loaded_db) &&
                loaded_db.magic == AUTH_DB_MAGIC)
            {
                db = loaded_db;
            }
        }
    }

    db_loaded = 1;
}

static int auth_find_user(const char* username)
{
    for (uint32_t i = 0; i < AUTH_MAX_USERS; i++)
    {
        if (db.users[i].in_use &&
            string_equals(
                db.users[i].username,
                username))
        {
            return (int)i;
        }
    }

    return -1;
}

static int auth_create_user(
    const char* username,
    const char* password
)
{
    if (db.count >= AUTH_MAX_USERS)
    {
        return 0;
    }

    if (auth_find_user(username) >= 0)
    {
        return 0;
    }

    int slot = -1;

    for (uint32_t i = 0; i < AUTH_MAX_USERS; i++)
    {
        if (!db.users[i].in_use)
        {
            slot = (int)i;
            break;
        }
    }

    if (slot < 0)
    {
        return 0;
    }

    auth_record_t* record = &db.users[slot];

    string_copy_bounded(
        record->username,
        username,
        AUTH_USERNAME_MAX
    );

    rng_get_bytes(
        record->salt,
        AUTH_SALT_SIZE
    );

    pbkdf2_hmac_sha256(
        (const uint8_t*)password,
        string_length(password),
        record->salt,
        AUTH_SALT_SIZE,
        AUTH_PBKDF2_ITERATIONS,
        record->hash,
        AUTH_HASH_SIZE
    );

    record->in_use = 1;
    db.count++;

    auth_db_save();

    return 1;
}

static int auth_verify_user(
    const char* username,
    const char* password
)
{
    int slot = auth_find_user(username);

    if (slot < 0)
    {
        return 0;
    }

    auth_record_t* record = &db.users[slot];

    uint8_t computed[AUTH_HASH_SIZE];

    pbkdf2_hmac_sha256(
        (const uint8_t*)password,
        string_length(password),
        record->salt,
        AUTH_SALT_SIZE,
        AUTH_PBKDF2_ITERATIONS,
        computed,
        AUTH_HASH_SIZE
    );

    int match =
        constant_time_equal(
            computed,
            record->hash,
            AUTH_HASH_SIZE
        );

    secure_wipe(computed, AUTH_HASH_SIZE);

    return match;
}

/*
 * ------------------------------------------------------------
 * Login-line editing (append-only: no cursor movement needed
 * for a single-line username/password prompt).
 * ------------------------------------------------------------
 */

static int auth_stage_is_secret(auth_stage_t s)
{
    return s == STAGE_SIGNUP_PASSWORD ||
           s == STAGE_SIGNUP_CONFIRM ||
           s == STAGE_LOGIN_PASSWORD;
}

static void auth_reset_line(void)
{
    line_length = 0;
    line_buffer[0] = '\0';
}

static void auth_prompt(const char* text)
{
    terminal_write(text);

    auth_reset_line();
}

void auth_init(void)
{
    if (!db_loaded)
    {
        auth_db_load();
    }
}

void auth_start(void)
{
    auth_init();

    logged_in = 0;

    if (db.count == 0)
    {
        terminal_write(
            "\nNo account exists yet on this "
            "disk.\n"
            "Let's create the first one - it "
            "will also become this machine's "
            "cryptographic identity.\n\n"
        );

        stage = STAGE_SIGNUP_USERNAME;

        auth_prompt("Choose a username: ");
    }
    else
    {
        terminal_write(
            "\nSpectreOS login\n"
        );

        stage = STAGE_LOGIN_USERNAME;

        auth_prompt("Username: ");
    }
}

int auth_login_active(void)
{
    return stage != STAGE_INACTIVE;
}

void auth_keyboard_character(char c)
{
    if (c < 32 || c > 126)
    {
        return;
    }

    if (line_length >=
        (sizeof(line_buffer) - 1))
    {
        return;
    }

    line_buffer[line_length] = c;
    line_length++;
    line_buffer[line_length] = '\0';

    terminal_putchar(
        auth_stage_is_secret(stage) ?
        '*' : c
    );
}

void auth_keyboard_backspace(void)
{
    if (line_length == 0)
    {
        return;
    }

    line_length--;
    line_buffer[line_length] = '\0';

    terminal_putchar('\b');
}

static void auth_finish_session(
    const char* username
)
{
    string_copy_bounded(
        active_username,
        username,
        AUTH_USERNAME_MAX
    );

    logged_in = 1;
    stage = STAGE_INACTIVE;

    auth_reset_line();

    secure_wipe(
        pending_password,
        sizeof(pending_password)
    );

    shell_init();
}

void auth_keyboard_enter(void)
{
    switch (stage)
    {
        case STAGE_SIGNUP_USERNAME:
        {
            if (line_length == 0)
            {
                terminal_write(
                    "\nUsername cannot be "
                    "empty.\n"
                );

                auth_prompt(
                    "Choose a username: "
                );

                return;
            }

            string_copy_bounded(
                pending_username,
                line_buffer,
                AUTH_USERNAME_MAX
            );

            terminal_write("\n");

            stage = STAGE_SIGNUP_PASSWORD;

            auth_prompt("Choose a password: ");

            return;
        }

        case STAGE_SIGNUP_PASSWORD:
        {
            if (line_length == 0)
            {
                terminal_write(
                    "\nPassword cannot be "
                    "empty.\n"
                );

                auth_prompt(
                    "Choose a password: "
                );

                return;
            }

            string_copy_bounded(
                pending_password,
                line_buffer,
                sizeof(pending_password)
            );

            terminal_write("\n");

            stage = STAGE_SIGNUP_CONFIRM;

            auth_prompt("Confirm password: ");

            return;
        }

        case STAGE_SIGNUP_CONFIRM:
        {
            terminal_write("\n");

            if (!string_equals(
                    line_buffer,
                    pending_password))
            {
                terminal_write(
                    "Passwords did not match. "
                    "Let's try again.\n\n"
                );

                secure_wipe(
                    pending_password,
                    sizeof(pending_password)
                );

                stage = STAGE_SIGNUP_USERNAME;

                auth_prompt(
                    "Choose a username: "
                );

                return;
            }

            if (!auth_create_user(
                    pending_username,
                    pending_password))
            {
                terminal_write(
                    "Could not create the "
                    "account (disk full?). "
                    "Try again.\n\n"
                );

                secure_wipe(
                    pending_password,
                    sizeof(pending_password)
                );

                stage = STAGE_SIGNUP_USERNAME;

                auth_prompt(
                    "Choose a username: "
                );

                return;
            }

            /*
             * The first account also becomes the
             * machine's cryptographic identity.
             */
            if (!identity_exists())
            {
                identity_generate(
                    pending_username
                );
            }

            terminal_write(
                "Account created.\n"
            );

            if (identity_is_loaded())
            {
                char fingerprint[
                    IDENTITY_ID_SIZE * 2 + 1
                ];

                identity_get_fingerprint_hex(
                    fingerprint,
                    sizeof(fingerprint)
                );

                terminal_write(
                    "Identity fingerprint: "
                );

                terminal_write(fingerprint);

                terminal_write("\n");
            }

            terminal_write(
                "\nWelcome, "
            );

            terminal_write(pending_username);

            terminal_write(".\n");

            auth_finish_session(
                pending_username
            );

            return;
        }

        case STAGE_LOGIN_USERNAME:
        {
            string_copy_bounded(
                pending_username,
                line_buffer,
                AUTH_USERNAME_MAX
            );

            terminal_write("\n");

            stage = STAGE_LOGIN_PASSWORD;

            auth_prompt("Password: ");

            return;
        }

        case STAGE_LOGIN_PASSWORD:
        {
            string_copy_bounded(
                pending_password,
                line_buffer,
                sizeof(pending_password)
            );

            terminal_write("\n");

            int ok =
                auth_verify_user(
                    pending_username,
                    pending_password
                );

            secure_wipe(
                pending_password,
                sizeof(pending_password)
            );

            if (!ok)
            {
                terminal_write(
                    "Access denied.\n\n"
                    "SpectreOS login\n"
                );

                stage = STAGE_LOGIN_USERNAME;

                auth_prompt("Username: ");

                return;
            }

            terminal_write(
                "Welcome back, "
            );

            terminal_write(pending_username);

            terminal_write(".\n");

            auth_finish_session(
                pending_username
            );

            return;
        }

        case STAGE_INACTIVE:
        default:
        {
            return;
        }
    }
}

void auth_logout(void)
{
    logged_in = 0;

    active_username[0] = '\0';

    auth_start();
}

int auth_is_logged_in(void)
{
    return logged_in;
}

const char* auth_current_username(void)
{
    return active_username;
}
