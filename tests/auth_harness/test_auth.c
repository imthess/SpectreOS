/*
 * Host-side tests for src/kernel/auth.c, compiled against the REAL
 * auth.c, sha256/hmac/pbkdf2, with the filesystem, terminal, shell and
 * identity layers replaced by small in-memory stubs. Drives auth the
 * same way the keyboard does (auth_keyboard_character / _enter).
 *
 * What this pins down:
 *   - password policy (minimum length), mismatch handling
 *   - new accounts use the current PBKDF2 work factor, stored per record
 *   - an account database written by the OLD code (fixed 4096
 *     iterations, previous on-disk layout) still logs in, and is
 *     transparently upgraded (new salt, new iteration count) on success
 *   - wrong password / unknown user are rejected, and an unknown user
 *     costs the same PBKDF2 work as a real one (no timing oracle)
 *   - weak-RNG path: no account or identity is created until the RNG
 *     reports itself seeded
 *   - corrupt/truncated database files are treated as "no accounts"
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "auth.h"
#include "pbkdf2.h"

/* ---------- stubs ---------- */
static char out[1 << 16];
static size_t out_len;

void terminal_write(const char* s)
{
    size_t n = strlen(s);
    if (out_len + n < sizeof out) { memcpy(out + out_len, s, n); out_len += n; out[out_len] = 0; }
}
void terminal_putchar(char c) { char t[2] = {c, 0}; terminal_write(t); }
static int shell_started;
void shell_init(void) { shell_started++; }

static int identity_flag;
int identity_exists(void) { return identity_flag; }
int identity_generate(const char* u) { (void)u; identity_flag = 1; return 1; }
int identity_is_loaded(void) { return identity_flag; }
void identity_get_fingerprint_hex(char* b, uint32_t n) { snprintf(b, n, "FINGERPRINT"); }

static int seeded = 1;
static uint32_t entropy_bits;
int rng_is_seeded(void) { return seeded; }
uint32_t rng_entropy_bits(void) { return entropy_bits; }
uint32_t rng_seed_threshold_bits(void) { return 256; }
static uint32_t rng_counter;
void rng_get_bytes(uint8_t* b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) b[i] = (uint8_t)(0x40 + (++rng_counter * 37u));
}
void secure_wipe(void* p, uint32_t n) { volatile uint8_t* v = p; while (n--) *v++ = 0; }

/* in-memory single-directory filesystem */
#define MAXF 4
static struct { char name[64]; uint8_t data[1 << 16]; uint32_t size; int used; } files[MAXF];
static struct { int file; uint32_t pos; int used; } handles[8];
static int find(const char* n) { for (int i = 0; i < MAXF; i++) if (files[i].used && !strcmp(files[i].name, n)) return i; return -1; }
int fs_exists(const char* n) { return find(n) >= 0; }
int fs_create(const char* n)
{
    if (find(n) >= 0) return 0;
    for (int i = 0; i < MAXF; i++) if (!files[i].used) { files[i].used = 1; strcpy(files[i].name, n); files[i].size = 0; return 1; }
    return 0;
}
int fs_open(const char* n)
{
    int f = find(n); if (f < 0) return -1;
    for (int h = 0; h < 8; h++) if (!handles[h].used) { handles[h].used = 1; handles[h].file = f; handles[h].pos = 0; return h; }
    return -1;
}
int fs_read(int h, void* buf, uint32_t n)
{
    uint32_t avail = files[handles[h].file].size - handles[h].pos;
    if (n > avail) n = avail;
    memcpy(buf, files[handles[h].file].data + handles[h].pos, n);
    handles[h].pos += n; return (int)n;
}
int fs_write(int h, const void* buf, uint32_t n)
{
    memcpy(files[handles[h].file].data + handles[h].pos, buf, n);
    handles[h].pos += n;
    if (handles[h].pos > files[handles[h].file].size) files[handles[h].file].size = handles[h].pos;
    return (int)n;
}
int fs_close(int h) { handles[h].used = 0; return 1; }

/* count PBKDF2 calls and the iteration counts requested */
void __real_pbkdf2_hmac_sha256(const uint8_t*, uint32_t, const uint8_t*, uint32_t, uint32_t, uint8_t*, uint32_t);
static int pbkdf2_calls;
static uint32_t pbkdf2_last_iter;
void __wrap_pbkdf2_hmac_sha256(const uint8_t* p, uint32_t pl, const uint8_t* s, uint32_t sl,
                               uint32_t it, uint8_t* o, uint32_t ol)
{
    pbkdf2_calls++; pbkdf2_last_iter = it;
    __real_pbkdf2_hmac_sha256(p, pl, s, sl, it, o, ol);
}

/* ---------- harness ---------- */
static int passed, failed;
static void check(int ok, const char* name)
{
    if (ok) { passed++; printf("  PASS  %s\n", name); }
    else { failed++; printf("  FAIL  %s\n", name); }
}
static int said(const char* s) { return strstr(out, s) != NULL; }
static void clear_out(void) { out_len = 0; out[0] = 0; }

static void type_line(const char* s)
{
    for (; *s; s++) auth_keyboard_character(*s);
    auth_keyboard_enter();
}

/* Mirrors the on-disk layouts defined privately in auth.c. */
typedef struct { char username[AUTH_USERNAME_MAX]; uint8_t salt[AUTH_SALT_SIZE]; uint8_t hash[AUTH_HASH_SIZE]; uint32_t in_use; } rec_v1_t;
typedef struct { uint32_t magic, count; rec_v1_t users[AUTH_MAX_USERS]; } db_v1_t;
typedef struct { char username[AUTH_USERNAME_MAX]; uint8_t salt[AUTH_SALT_SIZE]; uint8_t hash[AUTH_HASH_SIZE]; uint32_t in_use; uint32_t iterations; } rec_v2_t;
typedef struct { uint32_t magic, count; rec_v2_t users[AUTH_MAX_USERS]; } db_v2_t;

#define MAGIC_V1 0x53415554u
#define MAGIC_V2 0x53415532u

/* auth.c keeps its state in statics, so each scenario runs in a fresh
 * process image via fork-free reset helpers: we simply reload by calling
 * auth_logout()/auth_start() and replacing the file contents, and use
 * auth_reload_for_test() if present. Here we rely on auth_init() only
 * loading once, so scenarios that change the file use a new process. */

static void reset_world(void)
{
    memset(files, 0, sizeof files);
    memset(handles, 0, sizeof handles);
    identity_flag = 0; seeded = 1; entropy_bits = 0; shell_started = 0;
    clear_out();
}

static db_v2_t* load_v2(void)
{
    int f = find("users.dat");
    if (f < 0 || files[f].size != sizeof(db_v2_t)) return NULL;
    return (db_v2_t*)files[f].data;
}

static void scenario_signup(void)
{
    printf("== signup ==\n");
    reset_world();
    auth_init();
    auth_start();
    check(said("No account exists"), "empty database offers account creation");

    type_line("alice");
    clear_out(); type_line("short");
    check(said("at least 8"), "password shorter than 8 characters is rejected");
    clear_out(); type_line("hunter2hunter2");
    type_line("not-the-same");
    check(said("did not match"), "mismatched confirmation is rejected");
    check(find("users.dat") < 0 || load_v2() == NULL || load_v2()->count == 0,
          "nothing persisted after a failed signup");

    type_line("alice"); type_line("hunter2hunter2");
    pbkdf2_calls = 0;
    clear_out(); type_line("hunter2hunter2");
    check(said("Account created."), "matching confirmation creates the account");
    check(auth_is_logged_in() && !auth_login_active(), "session starts after signup");
    check(shell_started == 1, "shell is started once");
    check(pbkdf2_calls == 1 && pbkdf2_last_iter == AUTH_PBKDF2_ITERATIONS,
          "new password hashed once at the current work factor");

    db_v2_t* db = load_v2();
    check(db && db->magic == MAGIC_V2 && db->count == 1, "database saved in the v2 layout");
    check(db && db->users[0].iterations == AUTH_PBKDF2_ITERATIONS,
          "iteration count is stored per record");
    check(db && memmem(db, sizeof *db, "hunter2hunter2", 14) == NULL,
          "plaintext password is not stored");
    check(identity_flag == 1, "first account also creates the identity");
}

static void scenario_weak_rng(void)
{
    printf("== weak RNG path ==\n");
    reset_world();
    seeded = 0; entropy_bits = 0;
    auth_init();
    auth_start();
    type_line("carol"); type_line("long-enough-pw"); clear_out(); type_line("long-enough-pw");
    check(said("Entropy:"), "unseeded RNG -> asks the user for keystroke entropy");
    check(!auth_is_logged_in(), "no session while waiting for entropy");
    check(find("users.dat") < 0 || (load_v2() && load_v2()->count == 0),
          "no account is minted from an unseeded RNG");
    check(identity_flag == 0, "no identity is minted from an unseeded RNG");

    entropy_bits = 128; auth_keyboard_character('x');
    check(!auth_is_logged_in(), "half the entropy is not enough");

    seeded = 1; entropy_bits = 256; auth_keyboard_character('y');
    check(auth_is_logged_in() && load_v2() && load_v2()->count == 1,
          "account created once the RNG reports seeded");
    check(identity_flag == 1, "identity created only after seeding");
}

static void write_file(const void* data, uint32_t n)
{
    fs_create("users.dat");
    int h = fs_open("users.dat");
    fs_write(h, data, n);
    fs_close(h);
}

static void scenario_legacy_upgrade(void)
{
    printf("== legacy (v1, 4096 iterations) database ==\n");
    reset_world();

    static db_v1_t old;
    memset(&old, 0, sizeof old);
    old.magic = MAGIC_V1; old.count = 1;
    strcpy(old.users[0].username, "bob");
    for (int i = 0; i < AUTH_SALT_SIZE; i++) old.users[0].salt[i] = (uint8_t)(i * 7 + 1);
    __real_pbkdf2_hmac_sha256((const uint8_t*)"legacy-pass-1", 13, old.users[0].salt, AUTH_SALT_SIZE,
                              AUTH_PBKDF2_LEGACY_ITERATIONS, old.users[0].hash, AUTH_HASH_SIZE);
    old.users[0].in_use = 1;
    write_file(&old, sizeof old);
    uint8_t old_salt[AUTH_SALT_SIZE]; memcpy(old_salt, old.users[0].salt, sizeof old_salt);
    uint8_t old_hash[AUTH_HASH_SIZE]; memcpy(old_hash, old.users[0].hash, sizeof old_hash);

    auth_init();
    auth_start();
    check(said("SpectreOS login") && !said("No account exists"), "old database is recognised (login, not signup)");

    type_line("bob"); clear_out(); type_line("wrong-pass-1");
    check(said("Access denied"), "wrong password against a legacy record is rejected");
    check(load_v2() == NULL, "database is not rewritten by a failed login");

    pbkdf2_calls = 0;
    type_line("bob"); clear_out(); type_line("legacy-pass-1");
    check(said("Welcome back, bob."), "correct password under the OLD hash still logs in");
    check(pbkdf2_calls == 2, "login = one verify at 4096 + one re-hash at the new factor");

    db_v2_t* db = load_v2();
    check(db && db->magic == MAGIC_V2, "database upgraded to the v2 layout on disk");
    check(db && db->users[0].iterations == AUTH_PBKDF2_ITERATIONS, "record now carries the current iteration count");
    check(db && memcmp(db->users[0].salt, old_salt, sizeof old_salt) != 0, "salt was refreshed");
    check(db && memcmp(db->users[0].hash, old_hash, sizeof old_hash) != 0, "hash was replaced");

    auth_logout();
    pbkdf2_calls = 0;
    type_line("bob"); clear_out(); type_line("legacy-pass-1");
    check(said("Welcome back, bob.") && pbkdf2_calls == 1 && pbkdf2_last_iter == AUTH_PBKDF2_ITERATIONS,
          "upgraded record verifies once at the new work factor");

    auth_logout();
    pbkdf2_calls = 0;
    type_line("nobody"); clear_out(); type_line("whatever-123");
    check(said("Access denied") && pbkdf2_calls == 1 && pbkdf2_last_iter == AUTH_PBKDF2_ITERATIONS,
          "unknown user costs the same PBKDF2 work as a real one");
}

static void scenario_corrupt(void)
{
    printf("== corrupt database ==\n");
    reset_world();
    uint8_t junk[100]; memset(junk, 0xAB, sizeof junk);
    write_file(junk, sizeof junk);
    auth_init(); auth_start();
    check(said("No account exists"), "garbage file is treated as 'no accounts', not trusted");
}

int main(int argc, char** argv)
{
    /* auth.c's database is cached in statics; run one scenario per process. */
    const char* which = argc > 1 ? argv[1] : "";
    if (!strcmp(which, "signup")) scenario_signup();
    else if (!strcmp(which, "weak_rng")) scenario_weak_rng();
    else if (!strcmp(which, "legacy")) scenario_legacy_upgrade();
    else if (!strcmp(which, "corrupt")) scenario_corrupt();
    else { fprintf(stderr, "usage: %s signup|weak_rng|legacy|corrupt\n", argv[0]); return 2; }
    printf("  -> %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
