/*
 * phase1_osal/osal_thread.c — thread hypercalls.
 *
 * IMPLEMENTED: osal_thread_create + osal_thread_join +
 * osal_thread_self.
 *
 * Each osal_thread is an AmigaOS Task plus a MsgPort. The task's
 * tc_UserData points at our osal_thread. When the task returns
 * from its entry function, it PutMsg's a completion sentinel to
 * the join port and Wait()s forever (waits for the OS to reap it).
 * Joiner blocks on the port until the sentinel arrives, then
 * frees the osal_thread struct.
 */

#include "netstack/osal.h"
#include "rumpuser/rumpuser.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/exectags.h>
#include <exec/tasks.h>
#include <string.h>

struct osal_thread {
    struct Task     *task;         /* AmigaOS task */
    struct Task     *joiner;       /* task that will call osal_thread_join */
    ULONG            join_sigmask; /* which signal bit to fire on completion */
    LONG             join_sigbit;  /* -1 if we don't own one; else AllocSignal result */
    osal_thread_fn   fn;
    void            *arg;
    volatile int     done;         /* set by task before signalling */
};

/* Task entry trampoline. Recovers osal_thread from tc_UserData,
 * calls the user's fn, signals joiner, spins in Wait until reaped.
 *
 * Everything here uses IExec-only primitives (safe from a raw
 * Task context). PutMsg + AllocSysObjectTags(ASOT_PORT) turned
 * out to be one primitive too many for the join round-trip
 * (hangs during WaitPort even when the trampoline reaches
 * PutMsg cleanly per shared-counter evidence). Direct Signal
 * is simpler and doesn't need any allocated message struct. */
static void
osal_thread_trampoline(void)
{
    struct Task *self = IExec->FindTask(NULL);
    struct osal_thread *t = (struct osal_thread *)self->tc_UserData;

    t->fn(t->arg);
    t->done = 1;
    IExec->Signal(t->joiner, t->join_sigmask);

    /* Wait forever — joiner will RemTask() us. */
    for (;;) IExec->Wait(0);
}

int
osal_thread_create(osal_thread_fn fn, void *arg,
                   const char *name, int priority,
                   struct osal_thread **out)
{
    if (!fn || !out) return -1;
    if (priority < -128 || priority > 127) priority = 0;

    struct osal_thread *t = osal_malloc(sizeof(*t), 16);
    if (!t) return -1;
    t->fn = fn; t->arg = arg; t->done = 0;
    t->joiner = IExec->FindTask(NULL);
    t->join_sigbit = IExec->AllocSignal(-1);
    if (t->join_sigbit < 0) { osal_free(t); return -1; }
    t->join_sigmask = 1UL << t->join_sigbit;

    /* 16 KB was way too small — BSD kernel threads (schedhog,
     * workqueue, callout) blow it during rump_init and trap with
     * "stackpointer beyond bounds". 512 KB matches the ballpark
     * NetBSD uses for kthread_create's default. */
    IExec->Forbid();
    t->task = (struct Task *)IExec->CreateTaskTags(
        name ? name : "osal-thread", priority,
        osal_thread_trampoline, 512 * 1024,
        TAG_END);
    if (t->task) t->task->tc_UserData = t;
    IExec->Permit();
    extern void osal_trace(const char *fmt, ...);
    osal_trace("[thread] '%s' prio=%d stack=%dKB task=%p\n",
               name ? name : "osal-thread", priority, 512, t->task);

    if (!t->task) {
        IExec->FreeSignal(t->join_sigbit);
        osal_free(t);
        return -1;
    }
    *out = t;
    return 0;
}

int
osal_thread_join(struct osal_thread *t)
{
    if (!t) return -1;

    /* Poll t->done with brief sleeps instead of waiting on a
     * signal. Signal-based join wasn't waking on OS4 sam460ex
     * in early testing; poll is dumb but reliable, and 10 ms
     * granularity is fine for a Phase 1 spike (Phase 2+ can
     * revisit if we care about wakeup latency). */
    while (!t->done) {
        osal_sleep_ns(10000000ULL);   /* 10 ms */
    }

    /* Task is now spinning in Wait(0). Kill it. */
    IExec->Forbid();
    IExec->RemTask(t->task);
    IExec->Permit();

    IExec->FreeSignal(t->join_sigbit);
    osal_free(t);
    return 0;
}

struct osal_thread *
osal_thread_self(void)
{
    struct Task *self = IExec->FindTask(NULL);
    /* Only reliable for tasks we spawned. Others return NULL. */
    return (struct osal_thread *)self->tc_UserData;
}

/* -------- rump hypercall shims --------------------------------- */

/* Rump thread ABI: fn returns a void *. Our osal API takes a void
 * fn(void*). Wrap via a small heap-allocated shim carrying the
 * real fn + arg. */
struct rump_wrap {
    void *(*f)(void *);
    void  *arg;
};

static void
rump_thread_shim(void *w)
{
    struct rump_wrap *rw = (struct rump_wrap *)w;
    void *(*f)(void *) = rw->f;
    void  *arg          = rw->arg;
    osal_free(rw);
    (void)f(arg);
}

int
rumpuser_thread_create(void *(*f)(void *), void *arg, const char *thrname,
                       int mustjoin, int priority, int cpuidx, void **cookiep)
{
    (void)mustjoin; (void)cpuidx;
    struct rump_wrap *rw = osal_malloc(sizeof(*rw), 16);
    if (!rw) return 12 /* ENOMEM */;
    rw->f = f; rw->arg = arg;

    struct osal_thread *t;
    if (osal_thread_create(rump_thread_shim, rw, thrname, priority, &t) != 0) {
        osal_free(rw);
        return 12;
    }
    if (cookiep) *cookiep = t;
    return 0;
}

void
rumpuser_thread_exit(void)
{
    /* The trampoline handles this — user fn just returns. If a
     * caller invokes this directly, spin forever (task is dead
     * to us but the OS reaper will get it). */
    for (;;) IExec->Wait(0);
}

int
rumpuser_thread_join(void *cookie)
{
    return osal_thread_join((struct osal_thread *)cookie);
}

/* -------- LWP tracking ----------------------------------------- */

/* Every rump thread has a struct lwp * that rump uses to track
 * per-thread state (mostly for locking). Stored as tc_UserData
 * on the AmigaOS Task. TODO. */

struct lwp;
static struct lwp *g_stub_curlwp;   /* single global for the initial thread */

extern void osal_trace(const char *fmt, ...);

void
rumpuser_curlwpop(int op, struct lwp *l)
{
    /* Ops (per NetBSD): 0=CREATE, 1=DESTROY, 2=SET, 3=CLEAR. */
    osal_trace("[curlwpop] op=%d l=%p\n", op, l);
    if (op == 2) g_stub_curlwp = l;
    else if (op == 3) g_stub_curlwp = NULL;
}

struct lwp *
rumpuser_curlwp(void)
{
    return g_stub_curlwp;
}
