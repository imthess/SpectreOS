#include <stdint.h>

#include "ethernet.h"
#include "net_byteorder.h"
#include "nic.h"
#include "arp.h"
#include "ip.h"

const uint8_t eth_broadcast_mac[ETH_ADDR_LEN] =
{
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

void eth_addr_copy(
    uint8_t* destination,
    const uint8_t* source
)
{
    for (uint32_t i = 0; i < ETH_ADDR_LEN; i++)
    {
        destination[i] = source[i];
    }
}

int eth_addr_equals(
    const uint8_t* a,
    const uint8_t* b
)
{
    for (uint32_t i = 0; i < ETH_ADDR_LEN; i++)
    {
        if (a[i] != b[i])
        {
            return 0;
        }
    }

    return 1;
}

void eth_build_header(
    uint8_t* out,
    const uint8_t dest[ETH_ADDR_LEN],
    const uint8_t src[ETH_ADDR_LEN],
    uint16_t ethertype
)
{
    eth_addr_copy(&out[0], dest);
    eth_addr_copy(&out[6], src);

    net_write16(&out[12], ethertype);
}

int eth_send(
    const uint8_t dest[ETH_ADDR_LEN],
    uint16_t ethertype,
    const uint8_t* payload,
    uint32_t payload_length
)
{
    int nic_id = nic_get_active();

    if (nic_id < 0)
    {
        return 0;
    }

    const nic_device_t* nic =
        nic_get((uint32_t)nic_id);

    if (nic == 0)
    {
        return 0;
    }

    if (payload_length > (ETH_MAX_FRAME - ETH_HEADER_LEN))
    {
        return 0;
    }

    uint8_t frame[ETH_MAX_FRAME];

    eth_build_header(
        frame,
        dest,
        nic->mac,
        ethertype
    );

    for (uint32_t i = 0; i < payload_length; i++)
    {
        frame[ETH_HEADER_LEN + i] = payload[i];
    }

    uint32_t frame_length =
        ETH_HEADER_LEN + payload_length;

    /*
     * Ethernet frames shorter than 60 bytes (before the 4-byte
     * CRC the hardware appends) are padded with zeros - most
     * NICs, including the RTL8139 driver here, do this
     * automatically, but padding here too costs nothing and
     * keeps this function correct even against a future driver
     * that doesn't.
     */
    while (frame_length < 60)
    {
        frame[frame_length] = 0;
        frame_length++;
    }

    return nic->send(frame, frame_length);
}

void eth_handle_frame(
    const uint8_t* frame,
    uint32_t length
)
{
    if (length < ETH_HEADER_LEN)
    {
        return;
    }

    uint16_t ethertype =
        net_read16(&frame[12]);

    const uint8_t* payload =
        &frame[ETH_HEADER_LEN];

    uint32_t payload_length =
        length - ETH_HEADER_LEN;

    if (ethertype == ETHERTYPE_ARP)
    {
        arp_handle_packet(
            frame,
            payload,
            payload_length
        );
    }
    else if (ethertype == ETHERTYPE_IPV4)
    {
        ip_handle_packet(
            frame,
            payload,
            payload_length
        );
    }
}
