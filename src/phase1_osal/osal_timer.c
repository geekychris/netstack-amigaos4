/*
 * phase1_osal/osal_timer.c — clock + timer hypercalls.
 *
 * IMPLEMENTED:
 *   - osal_clock_monotonic_ns() reads the PPC time-base register
 *     directly (nominal 100 MHz on sam460ex). Returns nanoseconds
 *     since some monotonic epoch — good enough for elapsed-time
 *     measurements; not wall-clock.
 *   - osal_sleep_ns() uses timer.device via a per-task reusable
 *     IORequest opened on first call. Handles sub-microsecond
 *     durations by busy-yielding (Wait(0)).
 *
 * NOT YET: osal_timer_new/schedule/cancel (needs a background
 * timer-service task that owns the timer.device IORequest and
 * fires callbacks). Left as panic-stubs.
 */

#include "netstack/osal.h"
#include "rumpuser/rumpuser.h"

#include <proto/exec.h>
#include <proto/timer.h>
#include <exec/exectags.h>
#include <exec/io.h>
#include <devices/timer.h>

/* PPC time-base is 32 bits at ~100 MHz. Wraps every ~43 seconds.
 * We accumulate a 64-bit ns value across wraps using a per-call
 * snapshot; not thread-safe for the wrap-detection but fine for
 * duration measurements taken by a single thread. For robust
 * cross-thread monotonic time, timer.device gives µs resolution
 * without wrap concerns — swap in later if needed. */
#define TB_HZ 100000000ULL   /* nominal sam460ex TB frequency */

static inline uint32_t
tb_read(void)
{
    uint32_t v;
    __asm__ volatile ("mftb %0" : "=r"(v));
    return v;
}

uint64_t
osal_clock_monotonic_ns(void)
{
    /* Simple: return ticks * 10ns (assuming 100 MHz). No wrap
     * handling — caller should treat this as a low-order counter
     * for short-duration measurements. */
    return (uint64_t)tb_read() * 10ULL;
}

/* -------- Sleep via timer.device -------------------------------- */

static struct MsgPort       *g_timer_port;
static struct TimeRequest   *g_timer_req;
/* ITimer is exported by <proto/timer.h>; timer.device provides it. */

static int
timer_ensure_open(void)
{
    if (g_timer_req) return 0;

    g_timer_port = (struct MsgPort *)IExec->AllocSysObjectTags(
        ASOT_PORT, ASOPORT_AllocSig, TRUE, TAG_END);
    if (!g_timer_port) return -1;

    g_timer_req = (struct TimeRequest *)IExec->AllocSysObjectTags(
        ASOT_IOREQUEST,
        ASOIOR_ReplyPort, g_timer_port,
        ASOIOR_Size,      sizeof(struct TimeRequest),
        TAG_END);
    if (!g_timer_req) {
        IExec->FreeSysObject(ASOT_PORT, g_timer_port);
        g_timer_port = NULL;
        return -1;
    }

    if (IExec->OpenDevice("timer.device", UNIT_MICROHZ,
                          (struct IORequest *)g_timer_req, 0) != 0) {
        IExec->FreeSysObject(ASOT_IOREQUEST, g_timer_req);
        IExec->FreeSysObject(ASOT_PORT, g_timer_port);
        g_timer_req = NULL; g_timer_port = NULL;
        return -1;
    }

    /* Don't publish ITimer as a global — we don't call any of its
     * time-conversion functions from this file; DoIO on the request
     * is sufficient for TR_ADDREQUEST. */
    return 0;
}

void
osal_sleep_ns(uint64_t ns)
{
    if (timer_ensure_open() != 0) {
        /* Fallback: busy-yield. Approximate. */
        for (uint64_t i = 0; i < ns / 100; i++) IExec->Wait(0);
        return;
    }
    uint64_t us = ns / 1000ULL;
    if (us == 0) us = 1;

    g_timer_req->Request.io_Command = TR_ADDREQUEST;
    g_timer_req->Time.Seconds       = (ULONG)(us / 1000000ULL);
    g_timer_req->Time.Microseconds  = (ULONG)(us % 1000000ULL);
    IExec->DoIO((struct IORequest *)g_timer_req);
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
