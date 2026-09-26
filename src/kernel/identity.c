#include <stdint.h>

#include "identity.h"
#include "sha256.h"
#include "hmac_sha256.h"
#include "rng.h"
#include "fs.h"
#include "secure_wipe.h"

#define IDENTITY_MAGIC 0x53504944u /* "SPID" */
#define IDENTITY_DOMAIN "SPECTREOS-ID"

typedef struct
{
    uint32_t magic;
    char username[IDENTITY_USERNAME_MAX];
    uint8_t seed[IDENTITY_SEED_SIZE];
    uint8_t identity_id[IDENTITY_ID_SIZE];

} identity_record_t;

static identity_record_t current;
static int loaded = 0;

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

static void compute_identity_id(
    identity_record_t* record
)
{
    sha256_ctx_t ctx;

    sha256_init(&ctx);

    sha256_update(
        &ctx,
        record->seed,
        IDENTITY_SEED_SIZE
    );

    sha256_update(
        &ctx,
        IDENTITY_DOMAIN,
        string_length(IDENTITY_DOMAIN)
    );

    sha256_update(
        &ctx,
        record->username,
        string_length(record->username)
    );

    sha256_final(
        &ctx,
        record->identity_id
    );
}

int identity_exists(void)
{
    return fs_exists(IDENTITY_FILE_NAME);
}

int identity_load(void)
{
    if (!fs_exists(IDENTITY_FILE_NAME))
    {
        return 0;
    }

    int fd = fs_open(IDENTITY_FILE_NAME);

    if (fd < 0)
    {
        return 0;
    }

    identity_record_t record;

    int read_bytes =
        fs_read(
            fd,
            &record,
            sizeof(record)
        );

    fs_close(fd);

    if (read_bytes != (int)sizeof(record) ||
        record.magic != IDENTITY_MAGIC)
    {
        return 0;
    }

    current = record;
    loaded = 1;

    return 1;
}

int identity_generate(const char* username)
{
    if (username == 0 ||
        username[0] == '\0')
    {
        return 0;
    }

    identity_record_t record;

    record.magic = IDENTITY_MAGIC;

    string_copy_bounded(
        record.username,
        username,
        IDENTITY_USERNAME_MAX
    );

    rng_get_bytes(
        record.seed,
        IDENTITY_SEED_SIZE
    );

    compute_identity_id(&record);

    /*
     * Persist. Create the file only if it doesn't exist yet -
     * the record size never changes, so writing from offset 0
     * over an existing file is a safe in-place overwrite.
     */
    if (!fs_exists(IDENTITY_FILE_NAME))
    {
        if (!fs_create(IDENTITY_FILE_NAME))
        {
            secure_wipe(&record, sizeof(record));

            return 0;
        }
    }

    int fd = fs_open(IDENTITY_FILE_NAME);

    if (fd < 0)
    {
        secure_wipe(&record, sizeof(record));

        return 0;
    }

    int written =
        fs_write(
            fd,
            &record,
            sizeof(record)
        );

    fs_close(fd);

    if (written != (int)sizeof(record))
    {
        secure_wipe(&record, sizeof(record));

        return 0;
    }

    current = record;
    loaded = 1;

    secure_wipe(&record, sizeof(record));

    return 1;
}

int identity_is_loaded(void)
{
    return loaded;
}

const char* identity_get_username(void)
{
    if (!loaded)
    {
        return "";
    }

    return current.username;
}

void identity_get_fingerprint_hex(
    char* out,
    uint32_t out_size
)
{
    static const char digits[] = "0123456789abcdef";

    if (out == 0 || out_size == 0)
    {
        return;
    }

    uint32_t needed =
        IDENTITY_ID_SIZE * 2 + 1;

    if (out_size < needed)
    {
        if (out_size > 0)
        {
            out[0] = '\0';
        }

        return;
    }

    if (!loaded)
    {
        out[0] = '\0';

        return;
    }

    for (uint32_t i = 0; i < IDENTITY_ID_SIZE; i++)
    {
        out[i * 2 + 0] =
            digits[(current.identity_id[i] >> 4) & 0xF];

        out[i * 2 + 1] =
            digits[current.identity_id[i] & 0xF];
    }

    out[IDENTITY_ID_SIZE * 2] = '\0';
}

void identity_prove(
    const uint8_t* challenge,
    uint32_t challenge_length,
    uint8_t out_proof[IDENTITY_ID_SIZE]
)
{
    if (!loaded)
    {
        for (uint32_t i = 0; i < IDENTITY_ID_SIZE; i++)
        {
            out_proof[i] = 0;
        }

        return;
    }

    hmac_sha256(
        current.seed,
        IDENTITY_SEED_SIZE,
        challenge,
        challenge_length,
        out_proof
    );
}
