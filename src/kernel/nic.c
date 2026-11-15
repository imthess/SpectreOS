#include <stdint.h>

#include "nic.h"

static nic_device_t devices[NIC_MAX];
static uint32_t device_count = 0;

static void string_copy_bounded(
    char* destination,
    const char* source,
    uint32_t max_length
)
{
    uint32_t i = 0;

    while (source[i] != '\0' &&
           i < (max_length - 1))
    {
        destination[i] = source[i];
        i++;
    }

    destination[i] = '\0';
}

void nic_init(void)
{
    for (uint32_t i = 0; i < NIC_MAX; i++)
    {
        devices[i].name[0] = '\0';
        devices[i].send = 0;
        devices[i].poll_receive = 0;
        devices[i].in_use = 0;
    }

    device_count = 0;
}

int nic_register(
    const char* name,
    const uint8_t mac[NIC_MAC_LEN],
    nic_send_fn send,
    nic_poll_receive_fn poll_receive
)
{
    if (name == 0 ||
        mac == 0 ||
        send == 0 ||
        poll_receive == 0)
    {
        return -1;
    }

    if (device_count >= NIC_MAX)
    {
        return -1;
    }

    uint32_t id = device_count;

    string_copy_bounded(
        devices[id].name,
        name,
        NIC_NAME_MAX
    );

    for (uint32_t i = 0; i < NIC_MAC_LEN; i++)
    {
        devices[id].mac[i] = mac[i];
    }

    devices[id].send = send;
    devices[id].poll_receive = poll_receive;
    devices[id].in_use = 1;

    device_count++;

    return (int)id;
}

uint32_t nic_count(void)
{
    return device_count;
}

const nic_device_t* nic_get(uint32_t id)
{
    if (id >= NIC_MAX || !devices[id].in_use)
    {
        return 0;
    }

    return &devices[id];
}

int nic_get_active(void)
{
    if (device_count == 0)
    {
        return -1;
    }

    return 0;
}
