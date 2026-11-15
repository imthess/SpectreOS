#ifndef SPECTREOS_NET_CONFIG_H
#define SPECTREOS_NET_CONFIG_H

#include <stdint.h>

#include "ethernet.h"

/*
 * Small shared state: the protocol layers (arp.c, ip.c, icmp.c,
 * udp.c) need to know this machine's own IP/MAC and default
 * gateway, but shouldn't each depend on net.c's higher-level
 * init/poll logic - that would be a circular dependency, since
 * net.c depends on all of them. This file is the one thing
 * everybody can depend on.
 */

void net_config_init(void);

uint32_t net_get_local_ip(void);
void net_set_local_ip(uint32_t ip);

uint32_t net_get_gateway_ip(void);
void net_set_gateway_ip(uint32_t ip);

uint32_t net_get_subnet_mask(void);
void net_set_subnet_mask(uint32_t mask);

/*
 * The active NIC's MAC address, or all-zero if no NIC is
 * registered yet.
 */
void net_get_local_mac(uint8_t out[ETH_ADDR_LEN]);

/*
 * Builds a 32-bit host-order IPv4 address from its four octets,
 * e.g. net_make_ip(10, 0, 2, 15).
 */
uint32_t net_make_ip(
    uint8_t a,
    uint8_t b,
    uint8_t c,
    uint8_t d
);

#endif
