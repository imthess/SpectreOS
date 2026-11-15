#include <stdint.h>

#include "rtl8139.h"
#include "pci.h"
#include "nic.h"
#include "ethernet.h"

/*
 * See the full risk note in rtl8139.h. Short version: this
 * programs real hardware/emulated-hardware registers, follows
 * the widely-documented RTL8139 sequence as closely as memory
 * of that documentation allows, and has not been verified
 * against an actual card or QEMU instance. Every bounded retry
 * loop below (rather than an unbounded "wait forever") is
 * deliberate: if a register never reports what the datasheet
 * says it should, this driver gives up and reports failure
 * instead of hanging the kernel.
 */

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

/* Register offsets from the I/O base (BAR0). */
#define REG_IDR0      0x00 /* MAC address, 6 bytes */
#define REG_TSD0      0x10 /* Transmit Status of Descriptor 0-3 */
#define REG_TSAD0     0x20 /* Transmit Start Address of Descriptor 0-3 */
#define REG_RBSTART   0x30 /* Receive Buffer Start Address */
#define REG_CAPR      0x38 /* Current Address of Packet Read */
#define REG_CR        0x37 /* Command Register */
#define REG_IMR       0x3C /* Interrupt Mask Register */
#define REG_ISR       0x3E /* Interrupt Status Register */
#define REG_RCR       0x44 /* Receive Configuration Register */
#define REG_CONFIG1   0x52

#define CR_BUFE  0x01
#define CR_TE    0x04
#define CR_RE    0x08
#define CR_RST   0x10

#define RCR_APM  (1u << 1) /* accept physical (our own) address */
#define RCR_AB   (1u << 3) /* accept broadcast */
#define RCR_WRAP (1u << 7)

#define RX_BUFFER_LEN     8192u
#define RX_BUFFER_PADDING 1792u /* WRAP overflow headroom (1500 MTU + slack) */
#define RX_BUFFER_TOTAL   (RX_BUFFER_LEN + RX_BUFFER_PADDING)

#define TX_DESCRIPTOR_COUNT 4
#define TX_WAIT_MAX_ITERATIONS 1000000u

static uint32_t io_base = 0;
static int present = 0;

/*
 * DMA buffers: this project's paging is a flat identity mapping
 * (virtual address == physical address - see paging.c), so a
 * plain static array's address is already a valid "physical"
 * address to hand to the card. That assumption is called out
 * explicitly here because it is the one thing about this driver
 * that depends on something outside this file.
 */
static uint8_t rx_buffer[RX_BUFFER_TOTAL]
    __attribute__((aligned(4)));

static uint8_t tx_buffer[TX_DESCRIPTOR_COUNT][ETH_MAX_FRAME]
    __attribute__((aligned(4)));

static uint32_t rx_offset = 0;
static uint32_t tx_next = 0;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int rtl8139_send(
    const uint8_t* frame,
    uint32_t length
)
{
    if (!present || frame == 0)
    {
        return 0;
    }

    if (length > ETH_MAX_FRAME)
    {
        return 0;
    }

    uint32_t slot = tx_next;
    tx_next = (tx_next + 1) % TX_DESCRIPTOR_COUNT;

    /*
     * Wait for this descriptor to report OWN=1 (available - set
     * by the card once any previous transmission on this slot
     * finished). Bounded: on the very first use of each slot
     * this is already true from reset, so this only actually
     * waits once the 4 slots have wrapped around.
     */
    uint32_t waited = 0;

    while (!(inl(io_base + REG_TSD0 + slot * 4) & (1u << 13)))
    {
        waited++;

        if (waited > TX_WAIT_MAX_ITERATIONS)
        {
            return 0;
        }
    }

    for (uint32_t i = 0; i < length; i++)
    {
        tx_buffer[slot][i] = frame[i];
    }

    outl(
        io_base + REG_TSAD0 + slot * 4,
        (uint32_t)(uintptr_t)tx_buffer[slot]
    );

    /*
     * Writing the length to TSD both clears OWN (handing the
     * descriptor to the card) and starts transmission.
     */
    outl(io_base + REG_TSD0 + slot * 4, length);

    return 1;
}

static uint32_t rtl8139_poll_receive(
    uint8_t* buffer,
    uint32_t max_length
)
{
    if (!present || buffer == 0)
    {
        return 0;
    }

    /*
     * BUFE (Buffer Empty) set means nothing is waiting.
     */
    if (inb(io_base + REG_CR) & CR_BUFE)
    {
        return 0;
    }

    /*
     * Per-packet header the card prepends: 2 bytes of status
     * flags, then 2 bytes of length (the received frame plus
     * the 4-byte CRC the card appended - not including this
     * 4-byte header itself).
     */
    uint16_t status =
        (uint16_t)rx_buffer[rx_offset] |
        ((uint16_t)rx_buffer[rx_offset + 1] << 8);

    uint16_t packet_length =
        (uint16_t)rx_buffer[rx_offset + 2] |
        ((uint16_t)rx_buffer[rx_offset + 3] << 8);

    /*
     * ROK (Receive OK, bit 0 of the status word). Sanity-bound
     * the length too - a desynchronized ring (from a driver bug
     * this file may well have, given the honesty note above)
     * should fail closed rather than copy a wild amount of data.
     */
    if (!(status & 0x0001) ||
        packet_length < 4 ||
        packet_length > (ETH_MAX_FRAME + 4))
    {
        return 0;
    }

    uint32_t payload_length =
        (uint32_t)packet_length - 4;

    uint32_t copy_length =
        (payload_length > max_length) ?
        max_length : payload_length;

    for (uint32_t i = 0; i < copy_length; i++)
    {
        buffer[i] =
            rx_buffer[
                (rx_offset + 4 + i) % RX_BUFFER_TOTAL
            ];
    }

    /*
     * Advance past this packet's 4-byte header plus its full
     * length (data + CRC), 4-byte aligned as the card requires,
     * then wrap within the logical ring size.
     */
    uint32_t next_offset =
        rx_offset + 4 + packet_length;

    next_offset = (next_offset + 3) & ~3u;
    next_offset %= RX_BUFFER_LEN;

    rx_offset = next_offset;

    /*
     * CAPR has a well-documented off-by-16 quirk: the value
     * written back must be 16 less than the actual offset, or
     * the card gets confused about how much free space remains.
     */
    outw(
        io_base + REG_CAPR,
        (uint16_t)(rx_offset - 16)
    );

    return copy_length;
}

static int find_device(
    uint8_t* out_bus,
    uint8_t* out_device,
    uint8_t* out_function
)
{
    uint32_t count = pci_device_count();

    for (uint32_t i = 0; i < count; i++)
    {
        const pci_device_t* device =
            pci_get_device(i);

        if (device == 0)
        {
            continue;
        }

        if (device->vendor_id == RTL8139_VENDOR_ID &&
            device->device_id == RTL8139_DEVICE_ID)
        {
            *out_bus = device->bus;
            *out_device = device->device;
            *out_function = device->function;

            return 1;
        }
    }

    return 0;
}

void rtl8139_init(void)
{
    uint8_t bus, dev, func;

    if (!find_device(&bus, &dev, &func))
    {
        return;
    }

    /*
     * Enable I/O space access (bit 0) and bus mastering (bit 2)
     * in the PCI command register. The upper 16 bits of this
     * dword are the (write-1-to-clear) status register - writing
     * zero there is always safe and never clears anything.
     */
    uint32_t command =
        pci_config_read_dword(bus, dev, func, 0x04);

    uint32_t new_command =
        (command & 0x0000FFFFu) | 0x00000005u;

    pci_config_write_dword(bus, dev, func, 0x04, new_command);

    uint32_t bar0 =
        pci_config_read_dword(bus, dev, func, 0x10);

    io_base = bar0 & 0xFFFFFFFCu;

    if (io_base == 0)
    {
        return;
    }

    /* Power on. */
    outb(io_base + REG_CONFIG1, 0x00);

    /* Software reset, then wait (bounded) for it to clear. */
    outb(io_base + REG_CR, CR_RST);

    uint32_t waited = 0;

    while (inb(io_base + REG_CR) & CR_RST)
    {
        waited++;

        if (waited > TX_WAIT_MAX_ITERATIONS)
        {
            return;
        }
    }

    for (uint32_t i = 0; i < RX_BUFFER_TOTAL; i++)
    {
        rx_buffer[i] = 0;
    }

    rx_offset = 0;
    tx_next = 0;

    outl(
        io_base + REG_RBSTART,
        (uint32_t)(uintptr_t)rx_buffer
    );

    /* Polling only - no interrupts requested. */
    outw(io_base + REG_IMR, 0x0000);

    /*
     * Accept our own unicast address and broadcast (needed for
     * ARP) only - deliberately not promiscuous and not
     * multicast, matching this OS's stated privacy-first intent
     * as well as being the minimum this stack actually needs.
     */
    outl(
        io_base + REG_RCR,
        RCR_APM | RCR_AB | RCR_WRAP
    );

    /* Enable transmit and receive. */
    outb(io_base + REG_CR, CR_TE | CR_RE);

    uint8_t mac[NIC_MAC_LEN];

    for (uint32_t i = 0; i < NIC_MAC_LEN; i++)
    {
        mac[i] = inb(io_base + REG_IDR0 + i);
    }

    present = 1;

    nic_register(
        "rtl8139",
        mac,
        rtl8139_send,
        rtl8139_poll_receive
    );
}
