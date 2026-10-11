#!/usr/bin/env bash
#
# Filesystem encryption + backward-compatibility regression test.
# No QEMU, nasm, or root needed - builds the real src/kernel/fs.c
# (and a frozen copy of the pre-encryption one) as ordinary userspace
# programs against a fake ATA disk backed by a plain file, so each
# separate process run behaves like a reboot with a real disk image.
#
# What it pins down - the things that cannot be checked by compiling:
#   * a freshly formatted disk is really encrypted at rest (no plaintext
#     anywhere in the raw image) AND still readable after a "reboot"
#     (this is the check that caught a real bug: a per-boot random CTR
#     nonce silently destroyed all data on the next mount);
#   * a legacy disk formatted before encryption existed still mounts,
#     reads, and accepts writes, with its on-disk format never touched,
#     and the OLD code can still read what the NEW code wrote;
#   * multi-sector files, deletion, and the kernel's real two-crypto-
#     slot boot arrangement all behave.
#
# Exit status 0 only if every check passed.

set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SRC="$ROOT/src"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

PASS=0
FAIL=0

check() {
    local name="$1"; shift
    if "$@" > "$WORK/out.txt" 2>&1; then
        PASS=$((PASS + 1))
        echo "  PASS  $name"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL  $name"
        sed 's/^/        /' "$WORK/out.txt"
    fi
}

COMMON="$SRC/kernel/blockdev.c $SRC/kernel/crypto_disk.c $SRC/kernel/aes256.c \
$SRC/kernel/sha256.c $SRC/kernel/secure_wipe.c $SRC/kernel/fast_mem.c \
$SRC/kernel/ramdisk.c $HERE/fake_ata.c $HERE/fake_rng.c $HERE/harness.c"

# -O0 deliberately: matches the kernel's own (unoptimized) build flags.
gcc -O0 -Wall -Wextra -Wno-unused-function -I "$SRC/include" \
    -o "$WORK/fs_new" "$SRC/kernel/fs.c" $COMMON || { echo "build (new) failed"; exit 1; }
gcc -O0 -Wall -Wextra -Wno-unused-function -DLEGACY_FS \
    -I "$HERE/legacy" -I "$SRC/include" \
    -o "$WORK/fs_old" "$HERE/legacy/fs.c" $COMMON || { echo "build (legacy) failed"; exit 1; }

new() { FAKE_DISK="$1" "$WORK/fs_new" "${@:2}"; }
old() { FAKE_DISK="$1" "$WORK/fs_old" "${@:2}"; }

# Mirror kernel.c's boot: the cenc0 RAM-disk self-test claims the first
# crypto slot before fs_init() claims the second.
export PRECREATE_CENC=1

echo "== fresh encrypted disk =="
E="$WORK/enc.img"
check "format + write small file"            new "$E" format_and_write
check "write multi-sector file"              new "$E" big_write
check "raw image encrypted, no plaintext"    python3 "$HERE/inspect.py" "$E" encrypted
check "reboot: small file intact"            new "$E" verify
check "reboot: multi-sector file intact"     new "$E" big_verify
check "delete a file"                        new "$E" delete_hello
check "reboot: deletion persisted"           new "$E" verify_deleted
check "reboot: other file still intact"      new "$E" big_verify

echo "== legacy (pre-encryption) disk under the new code =="
L="$WORK/legacy.img"
check "old code formats + writes"            old "$L" format_and_write
cp "$L" "$WORK/legacy_before.img"
check "new code mounts, reads old file"      new "$L" verify
check "new code writes multi-sector file"    new "$L" big_write
check "new code reads it back after reboot"  new "$L" big_verify
check "new code deletes a file"              new "$L" delete_hello
check "deletion persisted"                   new "$L" verify_deleted
check "legacy reserved[] never touched"      python3 "$HERE/inspect.py" "$L" legacy
check "old code still reads new code's data" old "$L" big_verify
check "legacy superblock byte-identical"     cmp -n 512 "$WORK/legacy_before.img" "$L"

echo
echo "filesystem regression: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
