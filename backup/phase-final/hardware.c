#include <stdint.h>

#include "hardware.h"

static void cpuid(
    uint32_t leaf,
    uint32_t subleaf,
    uint32_t* eax,
    uint32_t* ebx,
    uint32_t* ecx,
    uint32_t* edx
)
{
    __asm__ volatile (
        "cpuid"
        : "=a"(*eax),
          "=b"(*ebx),
          "=c"(*ecx),
          "=d"(*edx)
        : "a"(leaf),
          "c"(subleaf)
    );
}

static void copy_uint32_to_chars(
    char* destination,
    uint32_t value
)
{
    destination[0] = (char)(value & 0xFF);
    destination[1] = (char)((value >> 8) & 0xFF);
    destination[2] = (char)((value >> 16) & 0xFF);
    destination[3] = (char)((value >> 24) & 0xFF);
}

void hardware_cpu_detect(cpu_info_t* info)
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    if (info == 0)
    {
        return;
    }

    /*
     * Clear the structure.
     */
    for (uint32_t i = 0;
         i < sizeof(cpu_info_t);
         i++)
    {
        ((uint8_t*)info)[i] = 0;
    }

    /*
     * CPUID leaf 0:
     *
     * EAX = highest supported basic leaf
     *
     * EBX/EDX/ECX = CPU vendor string
     */
    cpuid(
        0,
        0,
        &eax,
        &ebx,
        &ecx,
        &edx
    );

    info->eax_max_basic = eax;

    copy_uint32_to_chars(
        &info->vendor[0],
        ebx
    );

    copy_uint32_to_chars(
        &info->vendor[4],
        edx
    );

    copy_uint32_to_chars(
        &info->vendor[8],
        ecx
    );

    info->vendor[12] = '\0';

    /*
     * Basic processor information.
     */
    if (info->eax_max_basic >= 1)
    {
        cpuid(
            1,
            0,
            &eax,
            &ebx,
            &ecx,
            &edx
        );

        info->stepping =
            eax & 0xF;

        info->model =
            (eax >> 4) & 0xF;

        info->family =
            (eax >> 8) & 0xF;

        /*
         * Extended model/family handling.
         */
        uint32_t base_family =
            (eax >> 8) & 0xF;

        uint32_t base_model =
            (eax >> 4) & 0xF;

        uint32_t extended_model =
            (eax >> 16) & 0xF;

        uint32_t extended_family =
            (eax >> 20) & 0xFF;

        if (base_family == 0x6 ||
            base_family == 0xF)
        {
            info->model =
                base_model |
                (extended_model << 4);
        }

        if (base_family == 0xF)
        {
            info->family =
                base_family +
                extended_family;
        }

        info->features_ecx = ecx;
        info->features_edx = edx;
    }

    /*
     * Extended CPUID leaves.
     *
     * 0x80000000 gives the highest
     * supported extended leaf.
     */
    cpuid(
        0x80000000,
        0,
        &eax,
        &ebx,
        &ecx,
        &edx
    );

    info->eax_max_extended = eax;

    /*
     * Processor brand string:
     *
     * 0x80000002
     * 0x80000003
     * 0x80000004
     */
    if (eax >= 0x80000004)
    {
        uint32_t* brand =
            (uint32_t*)info->brand;

        cpuid(
            0x80000002,
            0,
            &brand[0],
            &brand[1],
            &brand[2],
            &brand[3]
        );

        cpuid(
            0x80000003,
            0,
            &brand[4],
            &brand[5],
            &brand[6],
            &brand[7]
        );

        cpuid(
            0x80000004,
            0,
            &brand[8],
            &brand[9],
            &brand[10],
            &brand[11]
        );

        info->brand[48] = '\0';
    }
}
