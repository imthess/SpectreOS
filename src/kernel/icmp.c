#include <stdint.h>

#include "icmp.h"
#include "ip.h"
#include "net_byteorder.h"
#include "checksum.h"

typedef struct
{
    int outstanding;
    uint16_t identifier;
    uint16_t sequence;
    int reply_received;

} ping_state_t;

static ping_state_t ping_state;

void icmp_init(void)
{
    ping_state.outstanding = 0;
    ping_state.reply_received = 0;
}

int icmp_send_echo_request(
    uint32_t dest_ip,
    uint16_t identifier,
    uint16_t sequence
)
{
    uint8_t packet[ICMP_HEADER_LEN];

    packet[0] = ICMP_TYPE_ECHO_REQUEST;
    packet[1] = 0; /* code */

    net_write16(&packet[2], 0); /* checksum placeholder */
    net_write16(&packet[4], identifier);
    net_write16(&packet[6], sequence);

    uint16_t sum =
        internet_checksum(packet, ICMP_HEADER_LEN);

    net_write16(&packet[2], sum);

    ping_state.outstanding = 1;
    ping_state.identifier = identifier;
    ping_state.sequence = sequence;
    ping_state.reply_received = 0;

    return ip_send(
        dest_ip,
        IP_PROTO_ICMP,
        packet,
        ICMP_HEADER_LEN
    );
}

int icmp_reply_received(
    uint16_t identifier,
    uint16_t sequence
)
{
    return
        ping_state.reply_received &&
        ping_state.identifier == identifier &&
        ping_state.sequence == sequence;
}

void icmp_handle_packet(
    uint32_t source_ip,
    const uint8_t* payload,
    uint32_t length
)
{
    if (length < ICMP_HEADER_LEN)
    {
        return;
    }

    if (internet_checksum(payload, length) != 0)
    {
        return;
    }

    uint8_t type = payload[0];

    if (type == ICMP_TYPE_ECHO_REQUEST)
    {
        /*
         * Someone is pinging us - reply with the same
         * identifier/sequence/trailing data, only flipping the
         * type field and recomputing the checksum. Bounded to a
         * small on-stack buffer since nothing sends oversized
         * pings on this network.
         */
        uint8_t reply[576];

        uint32_t reply_length =
            (length > sizeof(reply)) ?
            sizeof(reply) : length;

        for (uint32_t i = 0; i < reply_length; i++)
        {
            reply[i] = payload[i];
        }

        reply[0] = ICMP_TYPE_ECHO_REPLY;
        reply[1] = 0;

        net_write16(&reply[2], 0);

        uint16_t sum =
            internet_checksum(reply, reply_length);

        net_write16(&reply[2], sum);

        ip_send(
            source_ip,
            IP_PROTO_ICMP,
            reply,
            reply_length
        );
    }
    else if (type == ICMP_TYPE_ECHO_REPLY)
    {
        uint16_t identifier = net_read16(&payload[4]);
        uint16_t sequence = net_read16(&payload[6]);

        if (ping_state.outstanding &&
            ping_state.identifier == identifier &&
            ping_state.sequence == sequence)
        {
            ping_state.reply_received = 1;
        }
    }
}
