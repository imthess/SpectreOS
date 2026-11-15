#ifndef SPECTREOS_NET_H
#define SPECTREOS_NET_H

#include <stdint.h>

/*
 * Ties together nic.c, net_config.c, ethernet.c, arp.c, ip.c,
 * icmp.c and udp.c, and brings up the rtl8139 driver.
 *
 * Network configuration is a hardcoded static assignment
 * matching QEMU's default user-mode (SLIRP) networking:
 *   local IP  10.0.2.15
 *   gateway   10.0.2.2
 *   mask      255.255.255.0
 * That is a genuinely common hobby-OS default for exactly this
 * reason (it's what `qemu-system-i386 -net nic -net user` hands
 * out), but it is NOT DHCP - there is no protocol here that
 * negotiates this with whatever network the machine is actually
 * on. On real hardware, or a QEMU setup using a tap/bridge
 * instead of user-mode networking, these values are almost
 * certainly wrong and will need to be changed here (or a real
 * DHCP client written - out of scope for this pass, same
 * category of work as this whole stack: real, but not free).
 */
void net_init(void);

/*
 * Drains up to a small bounded number of received frames per
 * call. Must be called periodically (see the caller in
 * kernel.c's main loop) since the NIC driver is polling-only -
 * see nic.h for why.
 */
void net_poll(void);

/*
 * 1 if a NIC was found and brought up successfully.
 */
int net_is_up(void);

#endif
