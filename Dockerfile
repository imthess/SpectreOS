# Reproducible build+test environment for SpectreOS.
#
# `make check` inside it runs everything: host unit tests, the
# filesystem harness, a headless QEMU boot, and a keyboard-driven
# QEMU session (signup, login, files, real network traffic).
#
# Build:  docker build -t spectreos-dev .
# Test:   docker run --rm -v "$(pwd)":/spectreos spectreos-dev make check
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
        python3 \
        python3-cryptography \
        make \
        bash \
        && rm -rf /var/lib/apt/lists/*

WORKDIR /spectreos

# Intentionally no COPY of the source here: the source is mounted
# in at `docker run` time (see the usage comment above), so
# editing files on the host and re-running `make check` doesn't
# require rebuilding this image - only changes to this file
# (new package requirements) do.

CMD ["bash"]
