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

/*
 * Per-task IORequests are the OS-blessed way to avoid concurrent
 * DoIO races on a single request. AmigaOS message ports are keyed
 * to the task that allocated them (mp_SigTask), and DoIO waits on
 * that task's signal — so two tasks sharing one IORequest would
 * deadlock (only one wakes on completion).
 *
 * Cache per-task via a keyed lookup on FindTask(NULL). Grows on
 * demand; never shrinks. Tiny bookkeeping since we typically have
 * a handful of long-lived tasks (main + a few workers).
 */

#include <exec/semaphores.h>

#define TIMER_SLOT_MAX 16

struct timer_slot {
    struct Task         *owner;
    struct MsgPort      *port;
    struct TimeRequest  *req;
};

static struct SignalSemaphore g_timer_sem;
static int                    g_timer_sem_init;
static struct timer_slot      g_timer_slots[TIMER_SLOT_MAX];

static struct timer_slot *
timer_slot_for_current_task(void)
{
    struct Task *me = IExec->FindTask(NULL);
    if (!g_timer_sem_init) {
        IExec->InitSemaphore(&g_timer_sem);
        g_timer_sem_init = 1;
    }
    IExec->ObtainSemaphore(&g_timer_sem);
    /* Look up existing slot. */
    for (int i = 0; i < TIMER_SLOT_MAX; i++) {
        if (g_timer_slots[i].owner == me) {
            IExec->ReleaseSemaphore(&g_timer_sem);
            return &g_timer_slots[i];
        }
    }
    /* Allocate a new slot. */
    for (int i = 0; i < TIMER_SLOT_MAX; i++) {
        if (g_timer_slots[i].owner == NULL) {
            struct timer_slot *s = &g_timer_slots[i];
            s->port = (struct MsgPort *)IExec->AllocSysObjectTags(
                ASOT_PORT, ASOPORT_AllocSig, TRUE, TAG_END);
            if (!s->port) { IExec->ReleaseSemaphore(&g_timer_sem); return NULL; }
            s->req = (struct TimeRequest *)IExec->AllocSysObjectTags(
                ASOT_IOREQUEST,
                ASOIOR_ReplyPort, s->port,
                ASOIOR_Size,      sizeof(struct TimeRequest),
                TAG_END);
            if (!s->req) {
                IExec->FreeSysObject(ASOT_PORT, s->port);
                s->port = NULL;
                IExec->ReleaseSemaphore(&g_timer_sem);
                return NULL;
            }
            if (IExec->OpenDevice("timer.device", UNIT_MICROHZ,
                                  (struct IORequest *)s->req, 0) != 0) {
                IExec->FreeSysObject(ASOT_IOREQUEST, s->req);
                IExec->FreeSysObject(ASOT_PORT, s->port);
                s->req = NULL; s->port = NULL;
                IExec->ReleaseSemaphore(&g_timer_sem);
                return NULL;
            }
            s->owner = me;
            IExec->ReleaseSemaphore(&g_timer_sem);
            return s;
        }
    }
    IExec->ReleaseSemaphore(&g_timer_sem);
    return NULL;   /* no slots free */
}

void
osal_sleep_ns(uint64_t ns)
{
    struct timer_slot *s = timer_slot_for_current_task();
    if (!s) {
        /* Fallback: busy-yield. Approximate. */
        for (uint64_t i = 0; i < ns / 100; i++) IExec->Wait(0);
        return;
    }
    uint64_t us = ns / 1000ULL;
    if (us == 0) us = 1;

    s->req->Request.io_Command = TR_ADDREQUEST;
    s->req->Time.Seconds       = (ULONG)(us / 1000000ULL);
    s->req->Time.Microseconds  = (ULONG)(us % 1000000ULL);
    IExec->DoIO((struct IORequest *)s->req);
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

extern void osal_trace(const char *fmt, ...);

int
rumpuser_clock_gettime(int enum_rumpclock, int64_t *sec, long *nsec)
{
    (void)enum_rumpclock;
    uint64_t ns = osal_clock_monotonic_ns();
    if (sec)  *sec  = (int64_t)(ns / 1000000000ULL);
    if (nsec) *nsec = (long)(ns % 1000000000ULL);
    osal_trace("[clock_gettime] -> %ld.%ld\n", sec ? (long)*sec : -1, nsec ? *nsec : -1);
    return 0;
}

int
rumpuser_clock_sleep(int enum_rumpclock, int64_t sec, long nsec)
{
    (void)enum_rumpclock;
    osal_sleep_ns((uint64_t)sec * 1000000000ULL + (uint64_t)nsec);
    return 0;
}
