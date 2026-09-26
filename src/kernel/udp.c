#include <stdint.h>

#include "udp.h"
#include "ip.h"
#include "net_byteorder.h"
#include "net_config.h"
#include "checksum.h"

typedef struct
{
    uint16_t port;
    udp_receive_fn callback;
    int in_use;

} udp_binding_t;

static udp_binding_t bindings[UDP_MAX_BINDINGS];

void udp_init(void)
{
    for (uint32_t i = 0; i < UDP_MAX_BINDINGS; i++)
    {
        bindings[i].in_use = 0;
    }
}

int udp_bind(
    uint16_t port,
    udp_receive_fn callback
)
{
    if (callback == 0)
    {
        return 0;
    }

    for (uint32_t i = 0; i < UDP_MAX_BINDINGS; i++)
    {
        if (bindings[i].in_use &&
            bindings[i].port == port)
        {
            bindings[i].callback = callback;

            return 1;
        }
    }

    for (uint32_t i = 0; i < UDP_MAX_BINDINGS; i++)
    {
        if (!bindings[i].in_use)
        {
            bindings[i].port = port;
            bindings[i].callback = callback;
            bindings[i].in_use = 1;

            return 1;
        }
    }

    return 0;
}

void udp_unbind(uint16_t port)
{
    for (uint32_t i = 0; i < UDP_MAX_BINDINGS; i++)
    {
        if (bindings[i].in_use &&
            bindings[i].port == port)
        {
            bindings[i].in_use = 0;
        }
    }
}

static uint16_t udp_checksum(
    uint32_t source_ip,
    uint32_t dest_ip,
    const uint8_t* udp_packet,
    uint32_t udp_length
)
{
    uint8_t pseudo_header[12];

    net_write32(&pseudo_header[0], source_ip);
    net_write32(&pseudo_header[4], dest_ip);

    pseudo_header[8] = 0;
    pseudo_header[9] = IP_PROTO_UDP;

    net_write16(&pseudo_header[10], (uint16_t)udp_length);

    uint32_t sum =
        checksum_add(0, pseudo_header, 12);

    sum = checksum_add(sum, udp_packet, udp_length);

    uint16_t result = checksum_finish(sum);

    /*
     * RFC 768: a computed checksum of exactly 0 is sent as
     * all-ones instead, since 0 in the checksum field means "no
     * checksum was computed" on the receiving end.
     */
    return (result == 0) ? 0xFFFF : result;
}

int udp_send(
    uint32_t dest_ip,
    uint16_t dest_port,
    uint16_t src_port,
    const uint8_t* data,
    uint32_t length
)
{
    if (length > UDP_MAX_PAYLOAD)
    {
        return 0;
    }

    uint8_t packet[UDP_HEADER_LEN + UDP_MAX_PAYLOAD];

    uint16_t udp_length =
        (uint16_t)(UDP_HEADER_LEN + length);

    net_write16(&packet[0], src_port);
    net_write16(&packet[2], dest_port);
    net_write16(&packet[4], udp_length);
    net_write16(&packet[6], 0); /* checksum placeholder */

    for (uint32_t i = 0; i < length; i++)
    {
        packet[UDP_HEADER_LEN + i] = data[i];
    }

    uint16_t checksum =
        udp_checksum(
            net_get_local_ip(),
            dest_ip,
            packet,
            udp_length
        );

    net_write16(&packet[6], checksum);

    return ip_send(
        dest_ip,
        IP_PROTO_UDP,
        packet,
        udp_length
    );
}

void udp_handle_packet(
    uint32_t source_ip,
    const uint8_t* payload,
    uint32_t length
)
{
    if (length < UDP_HEADER_LEN)
    {
        return;
    }

    uint16_t source_port = net_read16(&payload[0]);
    uint16_t dest_port = net_read16(&payload[2]);
    uint16_t udp_length = net_read16(&payload[4]);
    uint16_t checksum = net_read16(&payload[6]);

    if (udp_length > length ||
        udp_length < UDP_HEADER_LEN)
    {
        return;
    }

    if (checksum != 0)
    {
        uint8_t verify_packet[UDP_HEADER_LEN + UDP_MAX_PAYLOAD];

        uint32_t verify_length =
            (udp_length > sizeof(verify_packet)) ?
            sizeof(verify_packet) : udp_length;

        for (uint32_t i = 0; i < verify_length; i++)
        {
            verify_packet[i] = payload[i];
        }

        net_write16(&verify_packet[6], 0);

        uint16_t recomputed =
            udp_checksum(
                source_ip,
                net_get_local_ip(),
                verify_packet,
                verify_length
            );

        if (recomputed != checksum)
        {
            return;
        }
    }

    const uint8_t* data = &payload[UDP_HEADER_LEN];
    uint32_t data_length = udp_length - UDP_HEADER_LEN;

    for (uint32_t i = 0; i < UDP_MAX_BINDINGS; i++)
    {
        if (bindings[i].in_use &&
            bindings[i].port == dest_port)
        {
            bindings[i].callback(
                source_ip,
                source_port,
                data,
                data_length
            );

            return;
        }
    }
}
