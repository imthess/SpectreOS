#include <stdint.h>

#include "hardware.h"

static cpu_info_t cpu_info;

static void mem_zero(
    void* ptr,
    uint32_t size
)
{
    uint8_t* p = (uint8_t*)ptr;

    while (size--)
    {
        *p++ = 0;
    }
}

static int cpuid_supported(void)
{
    uint32_t before;
    uint32_t after;

    __asm__ volatile (
        "pushfl\n"
        "popl %0\n"
        "movl %0, %1\n"
        "xorl $0x200000, %1\n"
        "pushl %1\n"
        "popfl\n"
        "pushfl\n"
        "popl %1\n"
        "pushl %0\n"
        "popfl\n"
        : "=r"(before),
          "=r"(after)
        :
        : "memory"
    );

    return ((before ^ after) & 0x200000U) != 0;
}

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

    mem_zero(
        &cpu_info,
        sizeof(cpu_info)
    );

    if (cpuid_supported() == 0)
    {
        return;
    }

    cpuid(
        0,
        0,
        &eax,
        &ebx,
        &ecx,
        &edx
    );

    uint32_t max_basic_leaf = eax;

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

    if (max_basic_leaf >= 1)
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

    cpuid(
        0x80000000,
        0,
        &eax,
        &ebx,
        &ecx,
        &edx
    );

    uint32_t max_extended_leaf = eax;

    if (max_extended_leaf >= 0x80000004)
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

        cpu_info.brand[48] = '\0';
    }

    cpu_info.available = 1;
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

int hardware_available(void)
{
    return cpu_info.available != 0;
}

uint32_t hardware_get_cpuid_max(void)
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    if (cpuid_supported() == 0)
    {
        return 0;
    }

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
