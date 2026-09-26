#include <stdint.h>

#include "net_config.h"
#include "nic.h"

static uint32_t local_ip = 0;
static uint32_t gateway_ip = 0;
static uint32_t subnet_mask = 0;

void net_config_init(void)
{
    local_ip = 0;
    gateway_ip = 0;
    subnet_mask = 0;
}

uint32_t net_get_local_ip(void)
{
    return local_ip;
}

void net_set_local_ip(uint32_t ip)
{
    local_ip = ip;
}

uint32_t net_get_gateway_ip(void)
{
    return gateway_ip;
}

void net_set_gateway_ip(uint32_t ip)
{
    gateway_ip = ip;
}

uint32_t net_get_subnet_mask(void)
{
    return subnet_mask;
}

void net_set_subnet_mask(uint32_t mask)
{
    subnet_mask = mask;
}

void net_get_local_mac(uint8_t out[ETH_ADDR_LEN])
{
    int nic_id = nic_get_active();

    if (nic_id >= 0)
    {
        const nic_device_t* nic =
            nic_get((uint32_t)nic_id);

        if (nic != 0)
        {
            for (uint32_t i = 0; i < ETH_ADDR_LEN; i++)
            {
                out[i] = nic->mac[i];
            }

            return;
        }
    }

    for (uint32_t i = 0; i < ETH_ADDR_LEN; i++)
    {
        out[i] = 0;
    }
}

uint32_t net_make_ip(
    uint8_t a,
    uint8_t b,
    uint8_t c,
    uint8_t d
)
{
    return
        ((uint32_t)a << 24) |
        ((uint32_t)b << 16) |
        ((uint32_t)c << 8) |
        ((uint32_t)d);
}
