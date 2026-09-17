#ifndef SPECTREOS_RAMDISK_H
#define SPECTREOS_RAMDISK_H

#include <stdint.h>

/*
 * In-memory block device. Exists mainly to prove that the
 * blockdev abstraction genuinely supports more than one kind
 * of backend, not just ATA - useful later for a scratch disk,
 * an initramfs, or testing the filesystem without real disk
 * I/O.
 *
 * Registers itself with blockdev_register() when initialized.
 * Returns the assigned device id, or -1 on failure.
 */
int ramdisk_init(void);

#endif
