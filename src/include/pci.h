#ifndef SPECTREOS_PCI_H
#define SPECTREOS_PCI_H

#include <stdint.h>

#define PCI_MAX_DEVICES 32

typedef struct
{
    uint8_t bus;
    uint8_t device;
    uint8_t function;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;

    uint8_t header_type;

} pci_device_t;

/*
 * Scans every bus/device/function via the legacy 0xCF8/0xCFC
 * configuration mechanism and records what responds. Safe to
 * call even on a system where PCI is absent - a non-present
 * slot always reads back vendor id 0xFFFF and is skipped.
 */
void pci_scan_bus(void);

uint32_t pci_device_count(void);

const pci_device_t* pci_get_device(uint32_t index);

/*
 * Human-readable class name for a PCI base class code, e.g.
 * 0x01 -> "Mass Storage", 0x02 -> "Network". Never returns 0.
 */
const char* pci_class_name(uint8_t class_code);

#endif
