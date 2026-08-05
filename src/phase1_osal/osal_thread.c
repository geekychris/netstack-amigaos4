/*
 * phase1_osal/osal_thread.c — thread hypercalls.
 *
 * STATUS: stub. Real impl uses CreateTaskTags + a per-thread
 * shutdown-reply MsgPort for join. Today: panics on create.
 */

#include "netstack/osal.h"
#include "rumpuser/rumpuser.h"

#include <proto/exec.h>

int
osal_thread_create(osal_thread_fn fn, void *arg,
                   const char *name, int priority,
                   struct osal_thread **out)
{
    (void)fn; (void)arg; (void)name; (void)priority;
    if (out) *out = NULL;
    osal_panic("osal_thread_create: not implemented (Phase 1 TODO)");
    return -1;
}

int
osal_thread_join(struct osal_thread *t)
{
    (void)t;
    osal_panic("osal_thread_join: not implemented");
    return -1;
}

struct osal_thread *
osal_thread_self(void)
{
    /* Real: FindTask(NULL) then cast/lookup. Stub: NULL. */
    return NULL;
}

/* -------- rump hypercall shims --------------------------------- */

int
rumpuser_thread_create(void *(*f)(void *), void *arg, const char *thrname,
                       int mustjoin, int priority, int cpuidx, void **cookiep)
{
    (void)f; (void)arg; (void)thrname; (void)mustjoin;
    (void)priority; (void)cpuidx;
    if (cookiep) *cookiep = NULL;
    osal_panic("rumpuser_thread_create: not implemented");
    return -1;
}

void
rumpuser_thread_exit(void)
{
    /* Real: exit the AmigaOS Task and signal any join waiter. */
    for (;;) IExec->Wait(0);
}

int
rumpuser_thread_join(void *cookie)
{
    (void)cookie;
    osal_panic("rumpuser_thread_join: not implemented");
    return -1;
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
