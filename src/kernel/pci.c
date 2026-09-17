#include <stdint.h>

#include "pci.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static pci_device_t devices[PCI_MAX_DEVICES];
static uint32_t device_count = 0;

static inline void outl(
    uint16_t port,
    uint32_t value
)
{
    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint32_t inl(
    uint16_t port
)
{
    uint32_t value;

    __asm__ volatile (
        "inl %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static uint32_t pci_config_read_dword(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset
)
{
    uint32_t address =
        ((uint32_t)1 << 31) |
        ((uint32_t)bus << 16) |
        ((uint32_t)(device & 0x1F) << 11) |
        ((uint32_t)(function & 0x07) << 8) |
        ((uint32_t)offset & 0xFC);

    outl(PCI_CONFIG_ADDRESS, address);

    return inl(PCI_CONFIG_DATA);
}

static uint16_t pci_read_vendor_id(
    uint8_t bus,
    uint8_t device,
    uint8_t function
)
{
    uint32_t dword =
        pci_config_read_dword(
            bus,
            device,
            function,
            0x00
        );

    return (uint16_t)(dword & 0xFFFF);
}

static void pci_probe_function(
    uint8_t bus,
    uint8_t device,
    uint8_t function
)
{
    uint16_t vendor_id =
        pci_read_vendor_id(
            bus,
            device,
            function
        );

    /*
     * 0xFFFF means nothing answered - no device here.
     */
    if (vendor_id == 0xFFFF)
    {
        return;
    }

    uint32_t dword0 =
        pci_config_read_dword(
            bus, device, function, 0x00
        );

    uint32_t dword2 =
        pci_config_read_dword(
            bus, device, function, 0x08
        );

    uint32_t dword3 =
        pci_config_read_dword(
            bus, device, function, 0x0C
        );

    if (device_count >= PCI_MAX_DEVICES)
    {
        return;
    }

    pci_device_t* entry =
        &devices[device_count];

    entry->bus = bus;
    entry->device = device;
    entry->function = function;

    entry->vendor_id =
        (uint16_t)(dword0 & 0xFFFF);

    entry->device_id =
        (uint16_t)((dword0 >> 16) & 0xFFFF);

    entry->revision =
        (uint8_t)(dword2 & 0xFF);

    entry->prog_if =
        (uint8_t)((dword2 >> 8) & 0xFF);

    entry->subclass =
        (uint8_t)((dword2 >> 16) & 0xFF);

    entry->class_code =
        (uint8_t)((dword2 >> 24) & 0xFF);

    entry->header_type =
        (uint8_t)((dword3 >> 16) & 0xFF);

    device_count++;
}

static void pci_probe_device(
    uint8_t bus,
    uint8_t device
)
{
    uint16_t vendor_id =
        pci_read_vendor_id(bus, device, 0);

    if (vendor_id == 0xFFFF)
    {
        return;
    }

    pci_probe_function(bus, device, 0);

    uint32_t dword3 =
        pci_config_read_dword(
            bus, device, 0, 0x0C
        );

    uint8_t header_type =
        (uint8_t)((dword3 >> 16) & 0xFF);

    /*
     * Bit 7 set means this is a multi-function device;
     * probe the remaining functions too.
     */
    if (header_type & 0x80)
    {
        for (uint8_t function = 1;
             function < 8;
             function++)
        {
            pci_probe_function(
                bus,
                device,
                function
            );
        }
    }
}

void pci_scan_bus(void)
{
    device_count = 0;

    for (uint32_t bus = 0; bus < 256; bus++)
    {
        for (uint32_t device = 0; device < 32; device++)
        {
            pci_probe_device(
                (uint8_t)bus,
                (uint8_t)device
            );
        }
    }
}

uint32_t pci_device_count(void)
{
    return device_count;
}

const pci_device_t* pci_get_device(uint32_t index)
{
    if (index >= device_count)
    {
        return 0;
    }

    return &devices[index];
}

const char* pci_class_name(uint8_t class_code)
{
    switch (class_code)
    {
        case 0x00: return "Unclassified";
        case 0x01: return "Mass Storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06: return "Bridge";
        case 0x07: return "Comms";
        case 0x08: return "System Peripheral";
        case 0x09: return "Input";
        case 0x0A: return "Docking";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial Bus";
        case 0x0D: return "Wireless";
        default:   return "Other";
    }
}
