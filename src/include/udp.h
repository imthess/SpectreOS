#ifndef SPECTREOS_UDP_H
#define SPECTREOS_UDP_H

#include <stdint.h>

#define UDP_HEADER_LEN    8
#define UDP_MAX_BINDINGS  4
#define UDP_MAX_PAYLOAD   1024

/*
 * Minimal socket-like API: bind a callback to a local port,
 * receive whatever arrives there. There is no connection state,
 * no listen/accept - this is closer to a raw UDP datagram
 * interface than a BSD socket, which is as far as "a minimal
 * socket API" goes without also building out a userland/kernel
 * boundary for it (see the project notes on why process
 * isolation is out of scope for now).
 */
typedef void (*udp_receive_fn)(
    uint32_t source_ip,
    uint16_t source_port,
    const uint8_t* data,
    uint32_t length
);

void udp_init(void);

/*
 * Sends `length` bytes to dest_ip:dest_port, from src_port.
 * Returns 1 if handed off to IP successfully.
 */
int udp_send(
    uint32_t dest_ip,
    uint16_t dest_port,
    uint16_t src_port,
    const uint8_t* data,
    uint32_t length
);

/*
 * Registers `callback` to receive datagrams sent to `port`.
 * Returns 1 on success, 0 if all UDP_MAX_BINDINGS slots are
 * already in use.
 */
int udp_bind(
    uint16_t port,
    udp_receive_fn callback
);

void udp_unbind(uint16_t port);

/*
 * Called by ip_handle_packet() for IP_PROTO_UDP packets.
 */
void udp_handle_packet(
    uint32_t source_ip,
    const uint8_t* payload,
    uint32_t length
);

#endif
