#ifndef SPECTREOS_RTL8139_H
#define SPECTREOS_RTL8139_H

#include <stdint.h>

/*
 * ============================================================
 * RTL8139 ETHERNET DRIVER - HONEST RISK NOTE, READ FIRST
 *
 * Everything else in this network stack (checksum.c, ethernet.c,
 * arp.c, ip.c, icmp.c, udp.c) is pure logic over byte buffers -
 * it was possible to unit-test it against known-answer vectors
 * or a reference implementation, the same way the crypto in
 * this project was verified.
 *
 * This file is fundamentally different: it programs a real
 * piece of hardware (or QEMU's emulation of one) through I/O
 * ports, and there is no way to unit-test port I/O side effects
 * in this environment - no QEMU, no real hardware, no way to
 * observe whether a byte written to a device register actually
 * did what the datasheet says it should. It compiles cleanly
 * and follows the widely-documented RTL8139 programming
 * sequence as faithfully as I could, but it has NOT been booted,
 * NOT been tested against a real or emulated card, and should be
 * treated as "written to spec, unverified" rather than "working".
 * If it doesn't work, the most likely culprits, in order, are:
 * the receive-buffer offset-by-16 quirk (see rtl8139_poll_receive
 * in the .c file), the transmit descriptor round-robin, or a
 * wrong assumption about identity-mapped memory being safe to
 * hand to the card as a DMA address (true today per paging.c,
 * would break if that ever changes).
 *
 * The chip: Realtek RTL8139, PCI vendor 0x10EC device 0x8139 -
 * QEMU's long-standing default `-net nic` model, chosen for
 * exactly that reason. Polling only (see nic.h for why).
 * ============================================================
 */

void rtl8139_init(void);

#endif
