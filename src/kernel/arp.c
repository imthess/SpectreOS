#include <stdint.h>

#include "arp.h"
#include "ethernet.h"
#include "net_byteorder.h"
#include "net_config.h"

#define ARP_HTYPE_ETHERNET 1
#define ARP_PTYPE_IPV4     0x0800
#define ARP_OP_REQUEST     1
#define ARP_OP_REPLY       2

typedef struct
{
    uint32_t ip;
    uint8_t mac[ETH_ADDR_LEN];
    uint32_t in_use;

} arp_entry_t;

static arp_entry_t cache[ARP_CACHE_SIZE];

static void cache_learn(
    uint32_t ip,
    const uint8_t mac[ETH_ADDR_LEN]
)
{
    int free_slot = -1;

    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (cache[i].in_use && cache[i].ip == ip)
        {
            eth_addr_copy(cache[i].mac, mac);

            return;
        }

        if (!cache[i].in_use && free_slot < 0)
        {
            free_slot = (int)i;
        }
    }

    /*
     * No existing entry. Use a free slot, or - if the cache is
     * full - simply overwrite slot 0. A tiny fixed-size cache
     * doesn't need a real eviction policy for what this project
     * currently does with it (talking to one gateway).
     */
    uint32_t slot =
        (free_slot >= 0) ?
        (uint32_t)free_slot : 0;

    cache[slot].ip = ip;
    eth_addr_copy(cache[slot].mac, mac);
    cache[slot].in_use = 1;
}

void arp_init(void)
{
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++)
    {
        cache[i].in_use = 0;
    }
}

void arp_request(uint32_t target_ip)
{
    uint8_t packet[ARP_HEADER_LEN];
    uint8_t local_mac[ETH_ADDR_LEN];

    net_get_local_mac(local_mac);

    net_write16(&packet[0], ARP_HTYPE_ETHERNET);
    net_write16(&packet[2], ARP_PTYPE_IPV4);

    packet[4] = ETH_ADDR_LEN;
    packet[5] = 4;

    net_write16(&packet[6], ARP_OP_REQUEST);

    eth_addr_copy(&packet[8], local_mac);
    net_write32(&packet[14], net_get_local_ip());

    /*
     * Target hardware address is unknown - all zero, as
     * required by RFC 826 for a request.
     */
    for (uint32_t i = 0; i < ETH_ADDR_LEN; i++)
    {
        packet[18 + i] = 0;
    }

    net_write32(&packet[24], target_ip);

    eth_send(
        eth_broadcast_mac,
        ETHERTYPE_ARP,
        packet,
        ARP_HEADER_LEN
    );
}

int arp_resolve(
    uint32_t ip,
    uint8_t out_mac[ETH_ADDR_LEN]
)
{
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (cache[i].in_use && cache[i].ip == ip)
        {
            eth_addr_copy(out_mac, cache[i].mac);

            return 1;
        }
    }

    return 0;
}

void arp_handle_packet(
    const uint8_t* eth_frame,
    const uint8_t* payload,
    uint32_t length
)
{
    if (length < ARP_HEADER_LEN)
    {
        return;
    }

    uint16_t htype = net_read16(&payload[0]);
    uint16_t ptype = net_read16(&payload[2]);
    uint16_t opcode = net_read16(&payload[6]);

    if (htype != ARP_HTYPE_ETHERNET ||
        ptype != ARP_PTYPE_IPV4)
    {
        return;
    }

    const uint8_t* sender_mac = &payload[8];
    uint32_t sender_ip = net_read32(&payload[14]);
    uint32_t target_ip = net_read32(&payload[24]);

    /*
     * Learn from any ARP traffic we see, request or reply -
     * standard behavior, saves a round trip later.
     */
    cache_learn(sender_ip, sender_mac);

    if (opcode == ARP_OP_REQUEST &&
        target_ip == net_get_local_ip())
    {
        uint8_t reply[ARP_HEADER_LEN];
        uint8_t local_mac[ETH_ADDR_LEN];

        net_get_local_mac(local_mac);

        net_write16(&reply[0], ARP_HTYPE_ETHERNET);
        net_write16(&reply[2], ARP_PTYPE_IPV4);

        reply[4] = ETH_ADDR_LEN;
        reply[5] = 4;

        net_write16(&reply[6], ARP_OP_REPLY);

        eth_addr_copy(&reply[8], local_mac);
        net_write32(&reply[14], net_get_local_ip());

        eth_addr_copy(&reply[18], sender_mac);
        net_write32(&reply[24], sender_ip);

        (void)eth_frame;

        eth_send(
            sender_mac,
            ETHERTYPE_ARP,
            reply,
            ARP_HEADER_LEN
        );
    }
}
