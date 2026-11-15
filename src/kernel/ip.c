#include <stdint.h>

#include "ip.h"
#include "ethernet.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "net_byteorder.h"
#include "net_config.h"
#include "checksum.h"

#define IP_VERSION_4 4
#define IP_DEFAULT_TTL 64

static uint16_t next_identification = 1;

void ip_init(void)
{
    next_identification = 1;
}

static uint32_t resolve_next_hop(uint32_t dest_ip)
{
    uint32_t mask = net_get_subnet_mask();
    uint32_t local = net_get_local_ip();

    int same_subnet =
        (mask == 0) ?
        1 :
        ((dest_ip & mask) == (local & mask));

    if (same_subnet)
    {
        return dest_ip;
    }

    return net_get_gateway_ip();
}

int ip_send(
    uint32_t dest_ip,
    uint8_t protocol,
    const uint8_t* payload,
    uint32_t payload_length
)
{
    if (payload_length > IP_MAX_PACKET)
    {
        return 0;
    }

    uint32_t next_hop =
        resolve_next_hop(dest_ip);

    uint8_t dest_mac[ETH_ADDR_LEN];

    if (!arp_resolve(next_hop, dest_mac))
    {
        arp_request(next_hop);

        return 0;
    }

    uint8_t packet[IP_HEADER_LEN + IP_MAX_PACKET];

    packet[0] =
        (uint8_t)((IP_VERSION_4 << 4) | 5);

    packet[1] = 0; /* DSCP/ECN */

    net_write16(
        &packet[2],
        (uint16_t)(IP_HEADER_LEN + payload_length)
    );

    net_write16(&packet[4], next_identification);
    next_identification++;

    net_write16(&packet[6], 0); /* flags/fragment offset: none */

    packet[8] = IP_DEFAULT_TTL;
    packet[9] = protocol;

    net_write16(&packet[10], 0); /* checksum placeholder */

    net_write32(&packet[12], net_get_local_ip());
    net_write32(&packet[16], dest_ip);

    uint16_t header_checksum =
        internet_checksum(packet, IP_HEADER_LEN);

    net_write16(&packet[10], header_checksum);

    for (uint32_t i = 0; i < payload_length; i++)
    {
        packet[IP_HEADER_LEN + i] = payload[i];
    }

    return eth_send(
        dest_mac,
        ETHERTYPE_IPV4,
        packet,
        IP_HEADER_LEN + payload_length
    );
}

void ip_handle_packet(
    const uint8_t* eth_frame,
    const uint8_t* payload,
    uint32_t length
)
{
    (void)eth_frame;

    if (length < IP_HEADER_LEN)
    {
        return;
    }

    uint8_t version = (uint8_t)(payload[0] >> 4);
    uint8_t ihl = (uint8_t)(payload[0] & 0x0F);

    if (version != IP_VERSION_4 || ihl < 5)
    {
        return;
    }

    uint32_t header_length = (uint32_t)ihl * 4;

    if (length < header_length)
    {
        return;
    }

    if (internet_checksum(payload, header_length) != 0)
    {
        /*
         * A correct header, checksummed over itself with the
         * checksum field included, always folds to exactly
         * zero. Anything else means transit corruption.
         */
        return;
    }

    uint16_t flags_and_offset =
        net_read16(&payload[6]);

    /*
     * Fragmentation is not supported - drop anything that is
     * fragmented (MF set, bit 0x2000) or is a non-first fragment
     * (offset != 0, low 13 bits) rather than silently
     * mis-handling it.
     */
    if ((flags_and_offset & 0x3FFF) != 0)
    {
        return;
    }

    uint16_t total_length = net_read16(&payload[2]);

    if (total_length > length ||
        total_length < header_length)
    {
        return;
    }

    uint8_t protocol = payload[9];
    uint32_t source_ip = net_read32(&payload[12]);
    uint32_t dest_ip = net_read32(&payload[16]);

    if (dest_ip != net_get_local_ip())
    {
        return;
    }

    const uint8_t* inner_payload =
        &payload[header_length];

    uint32_t inner_length =
        total_length - header_length;

    if (protocol == IP_PROTO_ICMP)
    {
        icmp_handle_packet(
            source_ip,
            inner_payload,
            inner_length
        );
    }
    else if (protocol == IP_PROTO_UDP)
    {
        udp_handle_packet(
            source_ip,
            inner_payload,
            inner_length
        );
    }
}
