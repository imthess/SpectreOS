#include <stdint.h>

#include "net.h"
#include "nic.h"
#include "net_config.h"
#include "ethernet.h"
#include "arp.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "rtl8139.h"

#define NET_POLL_MAX_FRAMES_PER_CALL 8

void net_init(void)
{
    nic_init();
    net_config_init();
    arp_init();
    ip_init();
    icmp_init();
    udp_init();

    rtl8139_init();

    if (nic_get_active() >= 0)
    {
        /*
         * QEMU user-mode networking defaults - see the note in
         * net.h. Wrong on real hardware or a differently
         * configured VM.
         */
        net_set_local_ip(
            net_make_ip(10, 0, 2, 15)
        );

        net_set_gateway_ip(
            net_make_ip(10, 0, 2, 2)
        );

        net_set_subnet_mask(
            net_make_ip(255, 255, 255, 0)
        );
    }
}

int net_is_up(void)
{
    return nic_get_active() >= 0;
}

void net_poll(void)
{
    int nic_id = nic_get_active();

    if (nic_id < 0)
    {
        return;
    }

    uint8_t frame[ETH_MAX_FRAME];

    for (uint32_t i = 0;
         i < NET_POLL_MAX_FRAMES_PER_CALL;
         i++)
    {
        const nic_device_t* nic =
            nic_get((uint32_t)nic_id);

        if (nic == 0)
        {
            return;
        }

        uint32_t length =
            nic->poll_receive(
                frame,
                sizeof(frame)
            );

        if (length == 0)
        {
            return;
        }

        eth_handle_frame(frame, length);
    }
}
