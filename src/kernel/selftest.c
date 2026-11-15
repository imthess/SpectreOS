#include <stdint.h>

#include "selftest.h"
#include "terminal.h"
#include "syscall.h"
#include "paging.h"
#include "memory.h"
#include "rng.h"
#include "sync.h"

static int passed;
static int failed;

static void check(int ok, const char* name)
{
    if (ok)
    {
        passed++;
        terminal_write("  PASS  ");
    }
    else
    {
        failed++;
        terminal_write("  FAIL  ");
    }

    terminal_write(name);
    terminal_write("\n");
}

static void write_number(int value)
{
    char digits[12];
    int n = 0;

    if (value == 0)
    {
        digits[n++] = '0';
    }

    while (value > 0 && n < 11)
    {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    }

    char text[12];

    for (int i = 0; i < n; i++)
    {
        text[i] = digits[n - 1 - i];
    }

    text[n] = '\0';
    terminal_write(text);
}

static void test_paging(void)
{
    terminal_write("paging range checks\n");

    uint8_t buffer[64];

    check(paging_range_ok((uint32_t)(uintptr_t)buffer, sizeof(buffer),
                          PAGING_PRESENT | PAGING_WRITE),
          "kernel stack buffer is present+writable");

    check(!paging_range_ok(0, 16, PAGING_PRESENT),
          "page 0 (NULL guard) is unmapped");

    check(!paging_range_ok(0x10000000u, 16, PAGING_PRESENT),
          "address far above the identity map is rejected");

    check(!paging_range_ok(0xFFFFF000u, 0x2000u, PAGING_PRESENT),
          "range wrapping past 4 GiB is rejected");

    check(paging_range_ok(0x10000000u, 0, PAGING_PRESENT),
          "zero-length range is trivially valid");

    check(paging_range_ok(0x3FF000u, 0x1000u, PAGING_PRESENT),
          "last mapped page (ends at 4 MiB) is accepted");

    check(!paging_range_ok(0x3FFFF0u, 0x100u, PAGING_PRESENT),
          "range straddling the end of the mapping is rejected");

    check(!paging_range_ok((uint32_t)(uintptr_t)buffer, 16,
                           PAGING_PRESENT | PAGING_USER),
          "kernel pages do not carry the user bit");
}

static void test_syscall_pointers(void)
{
    terminal_write("syscall pointer validation (ring-0 caller)\n");

    memory_info_t info;

    check(spectre_meminfo(&info) != 0,
          "valid output pointer is accepted");

    check(spectre_meminfo(0) == 0,
          "NULL output pointer is rejected");

    check(spectre_meminfo((memory_info_t*)0x10000000u) == 0,
          "unmapped output pointer is rejected");

    check(spectre_meminfo((memory_info_t*)0x3FFFFCu) == 0,
          "output struct straddling end of mapping is rejected");

    check(spectre_write(0) == 0,
          "NULL string to write is rejected");

    check(spectre_write((const char*)0x10000000u) == 0,
          "unmapped string to write is rejected");

    char long_name[100];

    for (int i = 0; i < 99; i++)
    {
        long_name[i] = 'a';
    }

    long_name[99] = '\0';

    check(spectre_open(long_name) == (uint32_t)-1,
          "over-long filename is rejected, not truncated (open)");

    check(spectre_create(long_name) == 0,
          "over-long filename is rejected, not truncated (create)");

    check(spectre_delete(0) == 0,
          "NULL filename is rejected (delete)");

    char buffer[16];

    check(spectre_read_file(0, buffer, 0xFFFFFFFFu) == (uint32_t)-1,
          "absurd read length is rejected");

    check(spectre_read_file(0, (void*)0xFFFFFFF0u, 64) == (uint32_t)-1,
          "read buffer that wraps the address space is rejected");

    check(spectre_list((char*)0x10000000u, 64) == (uint32_t)-1,
          "list into unmapped memory is rejected");
}

static void test_syscall_ring(void)
{
    terminal_write("syscall caller privilege (fabricated INT 0x80 frames)\n");

    memory_info_t info;
    uint32_t frame[11];

    /* pusha order: EDI ESI EBP ESP EBX EDX ECX EAX, then EIP CS EFLAGS */
    for (int i = 0; i < 11; i++)
    {
        frame[i] = 0;
    }

    frame[7] = SYS_MEMINFO;
    frame[4] = (uint32_t)(uintptr_t)&info;
    frame[9] = 0x08;   /* kernel code segment: RPL 0 */

    syscall_handler(frame);

    check(frame[7] != 0,
          "ring-0 caller may pass a kernel buffer");

    frame[7] = SYS_MEMINFO;
    frame[4] = (uint32_t)(uintptr_t)&info;
    frame[9] = 0x1B;   /* user code segment: RPL 3 */

    syscall_handler(frame);

    check(frame[7] == 0,
          "ring-3 caller is refused kernel memory");

    /* The privilege flag must not leak into the next kernel call. */
    check(spectre_meminfo(&info) != 0,
          "ring-0 calls still work after a ring-3 call");
}

static void test_rng(void)
{
    terminal_write("random number generator\n");

    uint8_t a[32];
    uint8_t b[32];

    rng_get_bytes(a, sizeof(a));
    rng_get_bytes(b, sizeof(b));

    int same = 1;

    for (uint32_t i = 0; i < sizeof(a); i++)
    {
        if (a[i] != b[i])
        {
            same = 0;
        }
    }

    check(!same, "consecutive outputs differ");

    int all_zero = 1;

    for (uint32_t i = 0; i < sizeof(a); i++)
    {
        if (a[i] != 0)
        {
            all_zero = 0;
        }
    }

    check(!all_zero, "output is not all zero");

    /* Odd lengths and zero length must not overrun or hang. */
    uint8_t guard[40];

    for (uint32_t i = 0; i < sizeof(guard); i++)
    {
        guard[i] = 0xA5;
    }

    rng_get_bytes(guard, 33);

    check(guard[33] == 0xA5 && guard[39] == 0xA5,
          "writes exactly the requested length (33 of 40)");

    rng_get_bytes(guard, 0);

    uint32_t before = rng_entropy_bits();

    for (uint32_t i = 1; i <= 128; i++)
    {
        volatile uint32_t spin = i * 997u;

        while (spin--)
        {
        }

        rng_add_entropy(i);
    }

    check(rng_entropy_bits() > before,
          "irregularly timed events are credited");

    check(rng_entropy_bits() - before <= 128,
          "at most one bit credited per event");
}

static void test_sync(void)
{
    terminal_write("synchronisation primitives\n");

    check(sync_self_test() == 1,
          "spinlock / mutex / semaphore basic semantics");

    spinlock_t lock;

    spinlock_init(&lock);

    check(spinlock_try_acquire(&lock) == 1,
          "try_acquire on a free lock succeeds");

    check(spinlock_try_acquire(&lock) == 0,
          "try_acquire on a held lock fails");

    spinlock_release(&lock);

    check(spinlock_try_acquire(&lock) == 1,
          "lock is acquirable again after release");

    spinlock_release(&lock);
}

int selftest_run(void)
{
    passed = 0;
    failed = 0;

    terminal_write("\n");

    test_paging();
    test_syscall_pointers();
    test_syscall_ring();
    test_rng();
    test_sync();

    terminal_write("\nselftest: ");
    write_number(passed);
    terminal_write(" passed, ");
    write_number(failed);
    terminal_write(" failed\n");

    return failed;
}
