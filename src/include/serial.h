#ifndef SPECTREOS_SERIAL_H
#define SPECTREOS_SERIAL_H

#include <stdint.h>

/*
 * COM1 (0x3F8) serial console. This exists specifically so an
 * automated test harness (QEMU with `-serial stdio` and no
 * display) can observe boot progress and panics without
 * screen-scraping the VGA text buffer - see scripts/ci_boot_test.sh
 * and the Makefile's `run-headless`/`test` targets.
 *
 * serial_init() runs a loopback self-test (a standard 16550 UART
 * technique: put the chip in loopback mode, write a known byte,
 * confirm it reads back) before trusting the port - some real
 * machines have no COM1 at all, and this project should degrade
 * gracefully rather than hang writing to a port nothing is
 * listening on. serial_write()/serial_present() reflect that:
 * every write is a no-op if the self-test failed.
 */
void serial_init(void);

int serial_present(void);

void serial_write(const char* text);

/*
 * Writes exactly one raw character, translating a bare '\n'
 * into "\r\n" the same way serial_write() does. Exposed
 * separately so terminal_putchar() (terminal.c) can mirror every
 * character it prints without building a temporary string per
 * call.
 */
void serial_putchar(char c);

#endif
