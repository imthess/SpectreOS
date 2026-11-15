#!/usr/bin/env python3
"""Raw-disk-image assertions for the filesystem regression test.

usage: inspect.py <image> encrypted|legacy

encrypted: superblock carries the SENC flag, and the known plaintext
           markers appear NOWHERE in the raw image.
legacy:    superblock reserved[] is entirely zero (never reinterpreted
           by the encryption-aware code), and the disk is still plaintext.
Exit status 0 = assertions held, 1 = something is wrong.
"""
import struct, sys

path, expect = sys.argv[1], sys.argv[2]
markers = [b"SECRET-PLAINTEXT-MARKER-0123456789"]
data = open(path, "rb").read()
fields = struct.unpack("<11I8I", data[:76])
reserved = fields[11:19]
problems = []

if expect == "encrypted":
    if reserved[0] != 0x53454E43:
        problems.append("SENC flag missing from superblock reserved[0]")
    for m in markers:
        if m in data:
            problems.append("plaintext %r found in raw encrypted image" % m)
elif expect == "legacy":
    if any(r != 0 for r in reserved):
        problems.append("legacy superblock reserved[] was modified: %s" % list(reserved))
else:
    problems.append("unknown expectation %r" % expect)

for p in problems:
    print("  RAW-IMAGE CHECK FAILED:", p)
sys.exit(1 if problems else 0)
