#ifndef SPECTREOS_NIC_H
#define SPECTREOS_NIC_H

#include <stdint.h>

/*
 * Same pattern as blockdev.h: any network card driver (rtl8139
 * being the one implemented here) registers itself through this
 * small interface instead of the rest of the network stack
 * depending on a specific driver.
 *
 * Polling only, deliberately: an interrupt-driven driver needs
 * a new PCI IRQ routed through the PIC/IDT, which is a second
 * hardware-interaction surface with its own failure modes on
 * top of the NIC programming itself. net_poll() (net.c) must be
 * called periodically - see the caller in kernel.c's main loop
 * and the note in rtl8139.h about what that trades away.
 */

#define NIC_MAX 2
#define NIC_NAME_MAX 16
#define NIC_MAC_LEN 6

typedef int (*nic_send_fn)(
    const uint8_t* frame,
    uint32_t length
);

/*
 * Copies one received frame into `buffer` (which must be at
 * least 1518 bytes - the maximum Ethernet frame) and returns
 * its length, or 0 if nothing has arrived.
 */
typedef uint32_t (*nic_poll_receive_fn)(
    uint8_t* buffer,
    uint32_t max_length
);

typedef struct
{
    char name[NIC_NAME_MAX];
    uint8_t mac[NIC_MAC_LEN];

    nic_send_fn send;
    nic_poll_receive_fn poll_receive;

    uint32_t in_use;

} nic_device_t;

void nic_init(void);

int nic_register(
    const char* name,
    const uint8_t mac[NIC_MAC_LEN],
    nic_send_fn send,
    nic_poll_receive_fn poll_receive
);

uint32_t nic_count(void);

const nic_device_t* nic_get(uint32_t id);

/*
 * Returns the id of the first registered NIC, or -1 if none.
 * This project only ever has zero or one real NIC, so "the
 * active one" is simply "the first one".
 */
int nic_get_active(void);

#endif
