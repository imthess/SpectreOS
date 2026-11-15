/* Shell commands for the network stack (netinfo, ping). */

#include <stdint.h>
#include "shell.h"
#include "terminal.h"
#include "syscall.h"
#include "thread.h"
#include "process.h"
#include "pmm.h"
#include "hardware.h"
#include "fs.h"
#include "pit.h"
#include "memory.h"
#include "nano.h"
#include "pci.h"
#include "blockdev.h"
#include "rng.h"
#include "crypto_disk.h"
#include "secure_wipe.h"
#include "identity.h"
#include "auth.h"
#include "net.h"
#include "net_config.h"
#include "arp.h"
#include "icmp.h"
#include "shell_internal.h"


static void shell_write_ip(uint32_t ip)
{
    shell_write_uint((ip >> 24) & 0xFF);
    terminal_write(".");
    shell_write_uint((ip >> 16) & 0xFF);
    terminal_write(".");
    shell_write_uint((ip >> 8) & 0xFF);
    terminal_write(".");
    shell_write_uint(ip & 0xFF);
}


static int shell_parse_ip(
    const char* text,
    uint32_t* out_ip
)
{
    uint32_t octets[4];
    uint32_t octet_index = 0;
    uint32_t value = 0;
    int have_digit = 0;

    for (uint32_t i = 0; ; i++)
    {
        char c = text[i];

        if (c >= '0' && c <= '9')
        {
            value = value * 10 + (uint32_t)(c - '0');
            have_digit = 1;

            if (value > 255)
            {
                return 0;
            }
        }
        else if (c == '.' || c == '\0')
        {
            if (!have_digit || octet_index >= 4)
            {
                return 0;
            }

            octets[octet_index] = value;
            octet_index++;
            value = 0;
            have_digit = 0;

            if (c == '\0')
            {
                break;
            }
        }
        else
        {
            return 0;
        }
    }

    if (octet_index != 4)
    {
        return 0;
    }

    *out_ip =
        net_make_ip(
            (uint8_t)octets[0],
            (uint8_t)octets[1],
            (uint8_t)octets[2],
            (uint8_t)octets[3]
        );

    return 1;
}


void shell_cmd_netinfo(void)
{
    terminal_write("\n");

    if (!net_is_up())
    {
        terminal_write(
            "No network interface found.\n"
        );

        return;
    }

    uint8_t mac[ETH_ADDR_LEN];
    net_get_local_mac(mac);

    terminal_write("MAC:     ");

    for (uint32_t i = 0; i < ETH_ADDR_LEN; i++)
    {
        static const char digits[] = "0123456789abcdef";

        terminal_putchar(digits[(mac[i] >> 4) & 0xF]);
        terminal_putchar(digits[mac[i] & 0xF]);

        if (i < ETH_ADDR_LEN - 1)
        {
            terminal_write(":");
        }
    }

    terminal_write("\nIP:      ");
    shell_write_ip(net_get_local_ip());

    terminal_write("\nGateway: ");
    shell_write_ip(net_get_gateway_ip());

    terminal_write("\nNetmask: ");
    shell_write_ip(net_get_subnet_mask());

    terminal_write("\n");
}




/*
 * NOTE ON THE TIMEOUT MECHANISM: this used to compare
 * pit_get_ticks() against a tick count, but that hung the
 * entire system - confirmed by reading interrupts.asm: the IDT
 * gates are 32-bit *interrupt* gates (idt.c, flags 0x8E), which
 * means the CPU hardware disables all interrupts on entry and
 * doesn't restore them until that interrupt handler returns via
 * iret. Since the entire shell command dispatch chain (keyboard
 * IRQ -> dispatch_enter() -> shell_keyboard_enter() ->
 * shell_execute() -> here) runs synchronously *inside* the
 * keyboard's own interrupt handler, and nothing in
 * interrupts.asm ever re-enables interrupts mid-handler, the
 * PIT's own timer interrupt could never fire while this function
 * was running - so pit_get_ticks() never advanced, and a
 * "wait until N ticks have passed" loop waited forever.
 *
 * Fixed by counting plain loop iterations instead, the same
 * pattern already used for every bounded wait in rtl8139.c. This
 * trades precise timing (an iteration takes a different amount
 * of wall-clock time on different host CPUs) for something that
 * provably cannot hang regardless of interrupt state - which
 * matters far more for a "give up gracefully" timeout than exact
 * timing does. Untuned/approximate: if this returns too fast or
 * too slow in practice, adjust PING_MAX_ITERATIONS.
 */
#define PING_MAX_ITERATIONS  3000000u
#define PING_ARP_RETRY_EVERY 50000u

void shell_cmd_ping(const char* argument)
{
    uint32_t dest_ip;

    if (!net_is_up())
    {
        terminal_write(
            "\nNo network interface available.\n"
        );

        return;
    }

    if (!shell_parse_ip(argument, &dest_ip))
    {
        terminal_write(
            "\nUsage: ping a.b.c.d\n"
        );

        return;
    }

    terminal_write("\nPinging ");
    shell_write_ip(dest_ip);
    terminal_write(" ... ");

    static uint16_t ping_sequence = 0;
    ping_sequence++;

    /*
     * NOTE ON RETRYING THE SEND: the first attempt almost always
     * fails to actually transmit anything - ip_send() needs the
     * destination's MAC address, which is essentially never
     * cached yet on a fresh boot; it fires an ARP request and
     * returns 0 rather than blocking. The previous version of
     * this function called icmp_send_echo_request() exactly once
     * and never noticed that failure, then sat waiting forever
     * for a reply to a packet that was never sent. Now it keeps
     * retrying the send (throttled, so as not to flood ARP
     * requests) until one attempt actually succeeds, and only
     * then starts waiting for a reply - and never calls
     * icmp_send_echo_request() again after that point, since it
     * resets the "reply received" state on every call and would
     * otherwise erase a reply that arrived between retries.
     */
    int sent = 0;
    uint32_t iteration = 0;

    while (iteration < PING_MAX_ITERATIONS)
    {
        net_poll();

        if (!sent)
        {
            if ((iteration % PING_ARP_RETRY_EVERY) == 0 &&
                icmp_send_echo_request(
                    dest_ip, 0xBEEF, ping_sequence))
            {
                sent = 1;
            }
        }
        else if (icmp_reply_received(0xBEEF, ping_sequence))
        {
            terminal_write("reply received\n");

            return;
        }

        iteration++;
    }

    terminal_write(
        sent ?
        "timed out (sent, but no reply came back)\n" :
        "timed out (destination MAC never resolved via ARP - "
        "no response to any ARP request)\n"
    );
}
