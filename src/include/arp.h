#ifndef SPECTREOS_ARP_H
#define SPECTREOS_ARP_H

#include <stdint.h>
#include "ethernet.h"

#define ARP_CACHE_SIZE 8
#define ARP_HEADER_LEN 28

void arp_init(void);

/*
 * Sends an ARP request for `target_ip` (a 32-bit host-order
 * IPv4 address). Does not block for the reply - call
 * arp_resolve() afterwards, after giving net_poll() a chance to
 * run, to check whether it arrived.
 */
void arp_request(uint32_t target_ip);

/*
 * Looks up `ip` in the ARP cache. Returns 1 and fills
 * `out_mac` if found, 0 otherwise.
 */
int arp_resolve(
    uint32_t ip,
    uint8_t out_mac[ETH_ADDR_LEN]
);

/*
 * Called by eth_handle_frame() for ETHERTYPE_ARP frames.
 * `eth_frame` is the full frame (used to read the sender's MAC
 * from the Ethernet header); `payload`/`length` are the ARP
 * packet itself.
 */
void arp_handle_packet(
    const uint8_t* eth_frame,
    const uint8_t* payload,
    uint32_t length
);

#endif
