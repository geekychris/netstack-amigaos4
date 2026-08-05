/*
 * phase1_osal/osal_timer.c — clock + timer hypercalls.
 *
 * STATUS: stub. Real impl uses timer.device with a per-thread
 * reusable IORequest for sleep/timeout, and TR_GETSYSTIME +
 * cached boot epoch for wall-clock reads.
 */

#include "netstack/osal.h"
#include "rumpuser/rumpuser.h"

#include <proto/exec.h>

uint64_t
osal_clock_monotonic_ns(void)
{
    /* TODO: read TB register directly (100 MHz on sam460ex) for
     * ns-resolution monotonic time. Placeholder returns 0. */
    return 0;
}

void
osal_sleep_ns(uint64_t ns)
{
    (void)ns;
    /* TODO: TR_ADDREQUEST on timer.device. Placeholder busy-yields. */
    IExec->Wait(0);
}

struct osal_timer *
osal_timer_new(osal_timer_fn fn, void *arg)
{
    (void)fn; (void)arg;
    return NULL;
}
void osal_timer_free(struct osal_timer *t)                     { (void)t; }
void osal_timer_schedule(struct osal_timer *t, uint64_t delay) { (void)t; (void)delay; }
void osal_timer_cancel(struct osal_timer *t)                   { (void)t; }

/* -------- rump hypercall shims --------------------------------- */

int
rumpuser_clock_gettime(int enum_rumpclock, int64_t *sec, long *nsec)
{
    (void)enum_rumpclock;
    uint64_t ns = osal_clock_monotonic_ns();
    if (sec)  *sec  = (int64_t)(ns / 1000000000ULL);
    if (nsec) *nsec = (long)(ns % 1000000000ULL);
    return 0;
}

int
rumpuser_clock_sleep(int enum_rumpclock, int64_t sec, long nsec)
{
    (void)enum_rumpclock;
    osal_sleep_ns((uint64_t)sec * 1000000000ULL + (uint64_t)nsec);
    return 0;
}
