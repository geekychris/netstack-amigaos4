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
#include <exec/exectags.h>
#include <exec/tasks.h>
#include <string.h>

struct osal_thread {
    struct Task     *task;      /* AmigaOS task */
    struct MsgPort  *join_port; /* joiner listens; task sends done */
    osal_thread_fn   fn;
    void            *arg;
    volatile int     done;      /* set by task before exiting */
};

/* Sentinel message the task PutMsg's to join_port when done. */
struct thread_done_msg {
    struct Message msg;
};

/* Task entry trampoline. Recovers osal_thread from tc_UserData,
 * calls the user's fn, signals completion. */
static void
osal_thread_trampoline(void)
{
    struct Task *self = IExec->FindTask(NULL);
    struct osal_thread *t = (struct osal_thread *)self->tc_UserData;

    IExec->DebugPrintF("[osal] tramp enter task=%p t=%p\n", self, t);
    t->fn(t->arg);
    IExec->DebugPrintF("[osal] tramp fn done, sending sentinel\n");
    t->done = 1;

    /* Send done sentinel. Allocated on stack — safe because we
     * Wait() forever after; the joiner reads the message before
     * killing us via RemTask/Signal. */
    struct thread_done_msg done;
    memset(&done, 0, sizeof(done));
    done.msg.mn_Node.ln_Type = NT_MESSAGE;
    done.msg.mn_Length       = sizeof(done);
    IExec->PutMsg(t->join_port, &done.msg);
    IExec->DebugPrintF("[osal] tramp PutMsg done; waiting\n");

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

    t->join_port = (struct MsgPort *)IExec->AllocSysObjectTags(
        ASOT_PORT, ASOPORT_AllocSig, TRUE, TAG_END);
    if (!t->join_port) { osal_free(t); return -1; }

    IExec->Forbid();
    t->task = (struct Task *)IExec->CreateTaskTags(
        name ? name : "osal-thread", priority,
        osal_thread_trampoline, 16384,
        TAG_END);
    if (t->task) t->task->tc_UserData = t;
    IExec->Permit();

    if (!t->task) {
        IExec->FreeSysObject(ASOT_PORT, t->join_port);
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

    IExec->DebugPrintF("[osal] join enter t=%p task=%p\n", t, t->task);

    /* Wait for the done sentinel. */
    (void)IExec->WaitPort(t->join_port);
    IExec->DebugPrintF("[osal] join WaitPort returned\n");
    struct Message *m = IExec->GetMsg(t->join_port);
    (void)m;
    IExec->DebugPrintF("[osal] join GetMsg got %p\n", m);

    /* Task is now spinning in Wait(0). Kill it. */
    IExec->Forbid();
    IExec->RemTask(t->task);
    IExec->Permit();
    IExec->DebugPrintF("[osal] join RemTask done\n");

    IExec->FreeSysObject(ASOT_PORT, t->join_port);
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

void
rumpuser_curlwpop(int op, struct lwp *l)
{
    /* Ops: 0=set, 1=get. Simplified: single-thread only. */
    (void)op;
    if (l) g_stub_curlwp = l;
}

struct lwp *
rumpuser_curlwp(void)
{
    return g_stub_curlwp;
}
