#!/usr/bin/env bash
# Builds and runs the host-side crypto/checksum known-answer tests
# against the real kernel sources. Needs only gcc.
set -euo pipefail
cd "$(dirname "$0")/../.."

out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

gcc -O1 -Wall -Wextra -Werror \
    -I src/include -I tests/crypto \
    tests/crypto/test_crypto.c \
    src/kernel/sha256.c src/kernel/hmac_sha256.c src/kernel/pbkdf2.c \
    src/kernel/aes256.c src/kernel/checksum.c \
    -o "$out/test_crypto"

"$out/test_crypto"
