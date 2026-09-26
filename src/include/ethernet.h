#ifndef SPECTREOS_ETHERNET_H
#define SPECTREOS_ETHERNET_H

#include <stdint.h>

#define ETH_ADDR_LEN     6
#define ETH_HEADER_LEN   14
#define ETH_MAX_FRAME    1518
#define ETH_MTU          1500

#define ETHERTYPE_IPV4   0x0800
#define ETHERTYPE_ARP    0x0806

extern const uint8_t eth_broadcast_mac[ETH_ADDR_LEN];

void eth_addr_copy(
    uint8_t* destination,
    const uint8_t* source
);

int eth_addr_equals(
    const uint8_t* a,
    const uint8_t* b
);

/*
 * Builds a 14-byte Ethernet header at `out` (which must have at
 * least ETH_HEADER_LEN bytes available).
 */
void eth_build_header(
    uint8_t* out,
    const uint8_t dest[ETH_ADDR_LEN],
    const uint8_t src[ETH_ADDR_LEN],
    uint16_t ethertype
);

/*
 * Sends `payload_length` bytes of `payload` wrapped in an
 * Ethernet frame addressed to `dest`, via the active NIC (see
 * nic.h). Returns 1 on success.
 */
int eth_send(
    const uint8_t dest[ETH_ADDR_LEN],
    uint16_t ethertype,
    const uint8_t* payload,
    uint32_t payload_length
);

/*
 * Called with one received frame (as returned by a NIC's
 * poll_receive). Parses the Ethernet header and dispatches to
 * arp_handle_packet() or ip_handle_packet() by ethertype.
 */
void eth_handle_frame(
    const uint8_t* frame,
    uint32_t length
);

#endif
