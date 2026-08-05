/*
 * tests/phase1/test_sleep.c — minimal osal_sleep_ns + clock verification.
 *
 * Sleeps 500 ms three times, reports elapsed clock ticks each time.
 * No threads. If this hangs, osal_sleep_ns or timer.device open is
 * the culprit — before we can trust any test that spawns workers.
 */

#include "netstack/osal.h"
#include <proto/exec.h>
#include <proto/dos.h>

int
main(int argc, char **argv)
{
    (void)argc; (void)argv;
    IDOS->Printf("test_sleep: start\n");
    IDOS->FFlush(IDOS->Output());

    for (int i = 0; i < 3; i++) {
        uint64_t t0 = osal_clock_monotonic_ns();
        osal_sleep_ns(500000000ULL);   /* 500 ms */
        uint64_t t1 = osal_clock_monotonic_ns();
        uint32_t elapsed_ms = (uint32_t)((t1 - t0) / 1000000ULL);
        IDOS->Printf("  iter %ld: elapsed %lu ms (want ~500)\n",
                     (LONG)i, (unsigned long)elapsed_ms);
        IDOS->FFlush(IDOS->Output());
    }
    IDOS->Printf("test_sleep: done\n");
    IDOS->FFlush(IDOS->Output());
    return 0;
}
