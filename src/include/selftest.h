#ifndef SPECTREOS_SELFTEST_H
#define SPECTREOS_SELFTEST_H

/*
 * In-kernel regression tests, run from the shell with `selftest`.
 * They execute the real kernel code in the real 32-bit environment
 * (the host build cannot: it has no 32-bit runtime), and are driven
 * automatically by tests/interactive/run.py.
 *
 * Prints one PASS/FAIL line per check and a final summary line
 * "selftest: N passed, M failed". Returns the number of failures.
 */
int selftest_run(void);

#endif
