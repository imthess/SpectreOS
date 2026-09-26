#!/usr/bin/env bash
#
# Boots SpectreOS headless under QEMU, captures the serial
# console log (see src/kernel/serial.c and the mirroring added
# to terminal_putchar()/the panic handler), and checks it against
# the boot markers every subsystem prints in kernel.c on success.
#
# Exit code is what makes this useful in CI: 0 only if every
# expected marker showed up and no panic marker did. Anything
# else - a hang QEMU has to be killed for, a marker missing, a
# panic - fails the job. This is what turns "the code compiles"
# into an actual pass/fail signal about whether the kernel boots.
#
# IMPORTANT CAVEAT: this has never actually been run. It was
# written and reviewed carefully, but there is no QEMU available
# in the environment that produced it (see README/DEVELOPMENT
# notes) - so this script itself is in the same "written to spec,
# unverified" category as the rtl8139 driver it tests. The first
# time this runs for real is on whatever machine actually has the
# toolchain installed - please treat a failure here as "check
# whether the script has a bug" as much as "check whether the
# kernel has a bug" until it's been run successfully at least
# once.

set -uo pipefail

ISO="iso/spectreos.iso"
DISK="disk.img"
LOG="build/boot_test.log"
TIMEOUT_SECONDS=30

mkdir -p build
rm -f "$LOG"

echo "Booting SpectreOS headless (timeout ${TIMEOUT_SECONDS}s)..."

timeout "$TIMEOUT_SECONDS" qemu-system-i386 \
    -cdrom "$ISO" \
    -drive file="$DISK",format=raw,if=ide,index=0,media=disk \
    -net nic,model=rtl8139 -net user \
    -display none \
    -serial file:"$LOG"

QEMU_EXIT=$?

if [ "$QEMU_EXIT" -eq 124 ]; then
    echo "NOTE: QEMU was killed after the ${TIMEOUT_SECONDS}s timeout."
    echo "This is expected if the kernel booted fine and just kept"
    echo "running (it never exits on its own) - the markers below"
    echo "are what actually determines pass/fail, not this exit code."
fi

if [ ! -s "$LOG" ]; then
    echo "FAIL: no serial output was captured at all."
    echo "Either QEMU failed to start, or the kernel never reached"
    echo "serial_init() (see kernel_main() in kernel.c), or COM1"
    echo "wasn't detected (serial_present() returned false, in which"
    echo "case every terminal_putchar() mirror is a silent no-op)."
    exit 1
fi

echo "----- captured boot log (${LOG}) -----"
cat "$LOG"
echo "---------------------------------------"

FAILED=0

if grep -q "KERNEL PANIC" "$LOG"; then
    echo "FAIL: kernel panicked during boot (see the register dump above)."
    FAILED=1
fi

# Markers with a single acceptable outcome - any variance from
# the literal "OK" string here is a real problem.
REQUIRED_OK_MARKERS=(
    "SpectreOS kernel starting"
    "GDT: OK"
    "IDT: OK"
    "Interrupts: OK"
    "PCI: OK"
    "Memory detection: OK"
    "Memory manager: OK"
    "Paging: OK"
    "Scheduler: OK"
    "Threading: OK"
    "System calls: OK"
    "ATA: OK"
    "RAM disk: OK"
    "Disk encryption: OK"
    "Filesystem: OK"
    "Accounts: OK"
    "Network: OK"
)

for marker in "${REQUIRED_OK_MARKERS[@]}"; do
    if ! grep -qF "$marker" "$LOG"; then
        echo "FAIL: expected boot marker not found: '${marker}'"
        FAILED=1
    fi
done

# RNG genuinely has two legitimate outcomes depending on whether
# the CPU/QEMU model exposes RDRAND - both mean the subsystem
# itself initialized fine, so only the line's presence matters,
# not which variant it is.
if ! grep -qF "RNG:" "$LOG"; then
    echo "FAIL: RNG subsystem never reported in (neither OK nor WARNING)"
    FAILED=1
fi

if [ "$FAILED" -ne 0 ]; then
    echo ""
    echo "Boot test FAILED."
    exit 1
fi

echo ""
echo "Boot test PASSED - every expected subsystem reported in, no panic."
exit 0
