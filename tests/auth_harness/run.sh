#!/usr/bin/env bash
# Host-side tests for src/kernel/auth.c (see test_auth.c). Needs only gcc.
set -euo pipefail
cd "$(dirname "$0")/../.."

out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

gcc -O2 -Wall -Wextra -D_GNU_SOURCE -I src/include \
    -Wl,--wrap=pbkdf2_hmac_sha256 \
    tests/auth_harness/test_auth.c \
    src/kernel/auth.c src/kernel/pbkdf2.c src/kernel/hmac_sha256.c src/kernel/sha256.c \
    -o "$out/test_auth"

status=0
for scenario in signup weak_rng legacy corrupt; do
    "$out/test_auth" "$scenario" || status=1
done
exit $status
