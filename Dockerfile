# Reproducible build+test environment for SpectreOS.
#
# This exists to close the one gap that's run through every part
# of this project so far: everything has been verified by
# compiling and, where possible, by logic/known-answer testing
# (crypto test vectors, checksum vectors, the boot-log grep
# logic in scripts/ci_boot_test.sh) - but nothing has ever
# actually been booted, because the environment that wrote this
# code has no `nasm`, no `grub-mkrescue`, and no `qemu`, and no
# network access to install them. This image is what makes that
# gap closeable: build it once, and `make test` inside it is a
# real boot, not a compile check.
#
# Build:  docker build -t spectreos-dev .
# Test:   docker run --rm -v "$(pwd)":/spectreos spectreos-dev make test
# Shell:  docker run --rm -it -v "$(pwd)":/spectreos spectreos-dev bash

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        gcc-multilib \
        nasm \
        grub-pc-bin \
        grub-common \
        xorriso \
        mtools \
        qemu-system-x86 \
        make \
        bash \
        && rm -rf /var/lib/apt/lists/*

WORKDIR /spectreos

# Intentionally no COPY of the source here: the source is mounted
# in at `docker run` time (see the usage comment above), so
# editing files on the host and re-running `make test` doesn't
# require rebuilding this image - only changes to this file
# (new package requirements) do.

CMD ["bash"]
