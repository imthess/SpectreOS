#include <stdint.h>

#include "hardware.h"

static cpu_info_t cpu_info;

static inline void cpuid(
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
        : "memory"
    );
}

static void copy_u32(
    char* destination,
    uint32_t value
)
{
    destination[0] =
        (char)(value & 0xFF);

    destination[1] =
        (char)((value >> 8) & 0xFF);

    destination[2] =
        (char)((value >> 16) & 0xFF);

    destination[3] =
        (char)((value >> 24) & 0xFF);
}

static void cpu_detect(void)
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    cpu_info.vendor[0] = '\0';
    cpu_info.brand[0] = '\0';

    cpuid(
        0,
        0,
        &eax,
        &ebx,
        &ecx,
        &edx
    );

    copy_u32(
        &cpu_info.vendor[0],
        ebx
    );

    copy_u32(
        &cpu_info.vendor[4],
        edx
    );

    copy_u32(
        &cpu_info.vendor[8],
        ecx
    );

    cpu_info.vendor[12] = '\0';

    uint32_t max_leaf = eax;

    if (max_leaf >= 1)
    {
        cpuid(
            1,
            0,
            &eax,
            &ebx,
            &ecx,
            &edx
        );

        cpu_info.stepping =
            eax & 0xF;

        cpu_info.model =
            (eax >> 4) & 0xF;

        cpu_info.family =
            (eax >> 8) & 0xF;

        uint32_t extended_model =
            (eax >> 16) & 0xF;

        uint32_t extended_family =
            (eax >> 20) & 0xFF;

        if (cpu_info.family == 6 ||
            cpu_info.family == 15)
        {
            cpu_info.model |=
                extended_model << 4;
        }

        if (cpu_info.family == 15)
        {
            cpu_info.family +=
                extended_family;
        }
    }

    uint32_t extended_max;

    cpuid(
        0x80000000,
        0,
        &extended_max,
        &ebx,
        &ecx,
        &edx
    );

    if (extended_max >= 0x80000004)
    {
        uint32_t* brand =
            (uint32_t*)cpu_info.brand;

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

        cpu_info.brand[48] =
            '\0';
    }
    else
    {
        cpu_info.brand[0] =
            '\0';
    }
}

void hardware_init(void)
{
    cpu_detect();
}

void hardware_get_cpu_info(
    cpu_info_t* info
)
{
    if (info == 0)
    {
        return;
    }

    *info = cpu_info;
}

uint32_t hardware_get_cpuid_max(void)
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    cpuid(
        0,
        0,
        &eax,
        &ebx,
        &ecx,
        &edx
    );

    return eax;
}
