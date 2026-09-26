#ifndef SPECTREOS_IP_H
#define SPECTREOS_IP_H

#include <stdint.h>

#define IP_HEADER_LEN   20
#define IP_PROTO_ICMP   1
#define IP_PROTO_UDP    17
#define IP_MAX_PACKET   (1500 - IP_HEADER_LEN)

void ip_init(void);

/*
 * Sends `payload_length` bytes as an IPv4 packet to `dest_ip`
 * (host-order). Resolves the next hop (dest_ip directly if it's
 * on the local subnet, otherwise the configured gateway) via
 * ARP. Returns 1 if the packet was handed to Ethernet, 0 if the
 * MAC address isn't known yet (an ARP request is sent
 * automatically - the caller should let net_poll() run a few
 * times and retry) or on any other failure.
 */
int ip_send(
    uint32_t dest_ip,
    uint8_t protocol,
    const uint8_t* payload,
    uint32_t payload_length
);

/*
 * Called by eth_handle_frame() for ETHERTYPE_IPV4 frames.
 */
void ip_handle_packet(
    const uint8_t* eth_frame,
    const uint8_t* payload,
    uint32_t length
);

#endif
