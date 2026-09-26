#ifndef SPECTREOS_ICMP_H
#define SPECTREOS_ICMP_H

#include <stdint.h>

#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8
#define ICMP_HEADER_LEN        8

void icmp_init(void);

/*
 * Sends an ICMP echo request (8 bytes of header, no payload
 * data) to `dest_ip`. Returns 1 if it was handed off
 * successfully (which, per ip_send()'s contract, may just mean
 * "an ARP request went out" the first time - see ip.h).
 */
int icmp_send_echo_request(
    uint32_t dest_ip,
    uint16_t identifier,
    uint16_t sequence
);

/*
 * 1 if an echo reply matching this identifier/sequence has been
 * received since the request was sent (call net_poll() between
 * sending and checking). Only the single most recent outstanding
 * request is tracked - fine for a "ping" command, not a
 * general-purpose multi-socket ICMP API.
 */
int icmp_reply_received(
    uint16_t identifier,
    uint16_t sequence
);

void icmp_handle_packet(
    uint32_t source_ip,
    const uint8_t* payload,
    uint32_t length
);

#endif
