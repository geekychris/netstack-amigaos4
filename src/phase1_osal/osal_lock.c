/*
 * phase1_osal/osal_lock.c — mutex / rwlock / cv hypercalls.
 *
 * STATUS: stubs. Uses SignalSemaphore for mutex/rwlock (works
 * but no priority inheritance) and a MsgPort+signal pair for cv.
 * Full contract-conformant implementation (recursion counts,
 * timedwait accuracy, cv broadcast semantics) is TODO.
 */

#include "netstack/osal.h"
#include "rumpuser/rumpuser.h"

#include <proto/exec.h>
#include <exec/exectags.h>
#include <exec/semaphores.h>

struct osal_mutex {
    struct SignalSemaphore sem;
};

struct osal_mutex *
osal_mutex_new(void)
{
    struct osal_mutex *m = osal_malloc(sizeof(*m), 16);
    if (m) IExec->InitSemaphore(&m->sem);
    return m;
}

void osal_mutex_free(struct osal_mutex *m)      { osal_free(m); }
void osal_mutex_lock(struct osal_mutex *m)      { IExec->ObtainSemaphore(&m->sem); }
int  osal_mutex_trylock(struct osal_mutex *m)   { return IExec->AttemptSemaphore(&m->sem); }
void osal_mutex_unlock(struct osal_mutex *m)    { IExec->ReleaseSemaphore(&m->sem); }

/* -------- Reader-writer lock: shared vs exclusive semaphore ---- */

struct osal_rwlock {
    struct SignalSemaphore sem;
};

struct osal_rwlock *
osal_rwlock_new(void)
{
    struct osal_rwlock *rw = osal_malloc(sizeof(*rw), 16);
    if (rw) IExec->InitSemaphore(&rw->sem);
    return rw;
}

void osal_rwlock_free(struct osal_rwlock *rw)   { osal_free(rw); }
void osal_rwlock_rlock(struct osal_rwlock *rw)  { IExec->ObtainSemaphoreShared(&rw->sem); }
void osal_rwlock_wlock(struct osal_rwlock *rw)  { IExec->ObtainSemaphore(&rw->sem); }
void osal_rwlock_unlock(struct osal_rwlock *rw) { IExec->ReleaseSemaphore(&rw->sem); }

/* -------- Condition variable: MsgPort + signal bit ------------- */

/*
 * Condition variable: a waiter list keyed by Task+SigMask.
 *
 * cv_wait allocates the caller a signal bit (SIGB_SINGLE, the
 * per-task private bit reserved for this kind of thing), pushes
 * {task, mask} onto the waiter list, releases the caller's
 * mutex, Wait()s for that signal, then re-acquires the mutex.
 *
 * cv_signal pops one waiter (FIFO) and Signal()s its task.
 * cv_broadcast pops all and signals each.
 *
 * The waiter list is a fixed-size ring; MAX_WAITERS is set high
 * enough for any Phase 1-2 workload. If exceeded we osal_panic
 * loudly — better than silent lost wakeups.
 *
 * SIGB_SINGLE is the standard convention for "this task is
 * blocking on ONE thing at a time, use bit 4". Using it avoids
 * calling AllocSignal repeatedly and having to store per-waiter
 * bits.
 */

#include <exec/execbase.h>   /* SIGF_SINGLE */

#define OSAL_CV_MAX_WAITERS 32

struct osal_cv_waiter {
    struct Task *task;
    ULONG        sigmask;
};

struct osal_cv {
    struct SignalSemaphore  guard;
    struct osal_cv_waiter   waiters[OSAL_CV_MAX_WAITERS];
    int                     head;    /* next slot to write */
    int                     tail;    /* next slot to read */
    int                     count;
};

struct osal_cv *
osal_cv_new(void)
{
    struct osal_cv *cv = osal_malloc(sizeof(*cv), 16);
    if (!cv) return NULL;
    IExec->InitSemaphore(&cv->guard);
    cv->head = cv->tail = cv->count = 0;
    return cv;
}

void
osal_cv_free(struct osal_cv *cv)
{
    if (!cv) return;
    osal_free(cv);
}

void
osal_cv_wait(struct osal_cv *cv, struct osal_mutex *mtx)
{
    struct Task *me = IExec->FindTask(NULL);
    ULONG mask = SIGF_SINGLE;

    /* Clear the signal first so a stale prior signal doesn't
     * cause a spurious immediate wake. */
    IExec->SetSignal(0, mask);

    IExec->ObtainSemaphore(&cv->guard);
    if (cv->count >= OSAL_CV_MAX_WAITERS) {
        IExec->ReleaseSemaphore(&cv->guard);
        osal_panic("osal_cv_wait: waiter overflow (raise OSAL_CV_MAX_WAITERS)");
    }
    cv->waiters[cv->head].task    = me;
    cv->waiters[cv->head].sigmask = mask;
    cv->head = (cv->head + 1) % OSAL_CV_MAX_WAITERS;
    cv->count++;
    IExec->ReleaseSemaphore(&cv->guard);

    osal_mutex_unlock(mtx);
    (void)IExec->Wait(mask);
    osal_mutex_lock(mtx);
}

int
osal_cv_timedwait(struct osal_cv *cv, struct osal_mutex *mtx, uint64_t timeout_ns)
{
    /* Simplified: wait via signal, but polled with brief sleeps
     * so we can bail on timeout. Same reliability rationale as
     * osal_thread_join. Real impl uses timer.device signal +
     * Wait(cv_sig | timer_sig). */
    struct Task *me = IExec->FindTask(NULL);
    ULONG mask = SIGF_SINGLE;
    IExec->SetSignal(0, mask);

    IExec->ObtainSemaphore(&cv->guard);
    if (cv->count >= OSAL_CV_MAX_WAITERS) {
        IExec->ReleaseSemaphore(&cv->guard);
        osal_panic("osal_cv_timedwait: waiter overflow");
    }
    cv->waiters[cv->head].task    = me;
    cv->waiters[cv->head].sigmask = mask;
    cv->head = (cv->head + 1) % OSAL_CV_MAX_WAITERS;
    cv->count++;
    IExec->ReleaseSemaphore(&cv->guard);

    osal_mutex_unlock(mtx);

    const uint64_t poll_ns = 10000000ULL;   /* 10 ms */
    uint64_t elapsed = 0;
    int rv = -60 /* ETIMEDOUT */;
    while (elapsed < timeout_ns) {
        ULONG got = IExec->SetSignal(0, 0);
        if (got & mask) { IExec->SetSignal(0, mask); rv = 0; break; }
        osal_sleep_ns(poll_ns);
        elapsed += poll_ns;
    }

    /* If we timed out, remove ourselves from the waiter list so
     * a future signal doesn't fire the wrong task. */
    if (rv != 0) {
        IExec->ObtainSemaphore(&cv->guard);
        for (int i = 0, idx = cv->tail; i < cv->count; i++,
             idx = (idx + 1) % OSAL_CV_MAX_WAITERS) {
            if (cv->waiters[idx].task == me &&
                cv->waiters[idx].sigmask == mask) {
                /* Compact by shifting subsequent entries left.
                 * Not the fastest but count is tiny. */
                for (int j = i; j < cv->count - 1; j++) {
                    int a = (cv->tail + j) % OSAL_CV_MAX_WAITERS;
                    int b = (a + 1) % OSAL_CV_MAX_WAITERS;
                    cv->waiters[a] = cv->waiters[b];
                }
                cv->head = (cv->head - 1 + OSAL_CV_MAX_WAITERS) % OSAL_CV_MAX_WAITERS;
                cv->count--;
                break;
            }
        }
        IExec->ReleaseSemaphore(&cv->guard);
    }

    osal_mutex_lock(mtx);
    return rv;
}

void
osal_cv_signal(struct osal_cv *cv)
{
    IExec->ObtainSemaphore(&cv->guard);
    if (cv->count > 0) {
        struct osal_cv_waiter w = cv->waiters[cv->tail];
        cv->tail = (cv->tail + 1) % OSAL_CV_MAX_WAITERS;
        cv->count--;
        IExec->ReleaseSemaphore(&cv->guard);
        IExec->Signal(w.task, w.sigmask);
    } else {
        IExec->ReleaseSemaphore(&cv->guard);
    }
}

void
osal_cv_broadcast(struct osal_cv *cv)
{
    IExec->ObtainSemaphore(&cv->guard);
    while (cv->count > 0) {
        struct osal_cv_waiter w = cv->waiters[cv->tail];
        cv->tail = (cv->tail + 1) % OSAL_CV_MAX_WAITERS;
        cv->count--;
        IExec->Signal(w.task, w.sigmask);
    }
    IExec->ReleaseSemaphore(&cv->guard);
}

/* -------- rump hypercall shims --------------------------------- */

extern void osal_trace(const char *fmt, ...);

void rumpuser_mutex_init(struct rumpuser_mtx **mtx, int flags) {
    (void)flags;
    *mtx = (struct rumpuser_mtx *)osal_mutex_new();
    osal_trace("[mtx_init] flags=%d -> %p\n", flags, *mtx);
}
void rumpuser_mutex_enter(struct rumpuser_mtx *mtx)  { osal_mutex_lock((struct osal_mutex *)mtx); }
void rumpuser_mutex_enter_nowrap(struct rumpuser_mtx *mtx) { osal_mutex_lock((struct osal_mutex *)mtx); }
int  rumpuser_mutex_tryenter(struct rumpuser_mtx *mtx) { return osal_mutex_trylock((struct osal_mutex *)mtx); }
void rumpuser_mutex_exit(struct rumpuser_mtx *mtx)   { osal_mutex_unlock((struct osal_mutex *)mtx); }
void rumpuser_mutex_destroy(struct rumpuser_mtx *mtx){ osal_mutex_free((struct osal_mutex *)mtx); }
void rumpuser_mutex_owner(struct rumpuser_mtx *mtx, struct lwp **lp) { (void)mtx; *lp = NULL; }

void rumpuser_rw_init(struct rumpuser_rw **rw)             { *rw = (struct rumpuser_rw *)osal_rwlock_new(); osal_trace("[rw_init] -> %p\n", *rw); }
void rumpuser_rw_enter(int type, struct rumpuser_rw *rw)   { (void)type; osal_rwlock_wlock((struct osal_rwlock *)rw); }
int  rumpuser_rw_tryenter(int t, struct rumpuser_rw *rw)   { (void)t; (void)rw; return 0; }
int  rumpuser_rw_tryupgrade(struct rumpuser_rw *rw)        { (void)rw; return 0; }
void rumpuser_rw_downgrade(struct rumpuser_rw *rw)         { (void)rw; }
void rumpuser_rw_exit(struct rumpuser_rw *rw)              { osal_rwlock_unlock((struct osal_rwlock *)rw); }
void rumpuser_rw_destroy(struct rumpuser_rw *rw)           { osal_rwlock_free((struct osal_rwlock *)rw); }
void rumpuser_rw_held(int t, struct rumpuser_rw *rw, int *heldp) { (void)t; (void)rw; *heldp = 0; }

void rumpuser_cv_init(struct rumpuser_cv **cv)             { *cv = (struct rumpuser_cv *)osal_cv_new(); osal_trace("[cv_init] -> %p\n", *cv); }
void rumpuser_cv_destroy(struct rumpuser_cv *cv)           { osal_cv_free((struct osal_cv *)cv); }
void rumpuser_cv_wait(struct rumpuser_cv *cv, struct rumpuser_mtx *mtx) {
    osal_cv_wait((struct osal_cv *)cv, (struct osal_mutex *)mtx);
}
void rumpuser_cv_wait_nowrap(struct rumpuser_cv *cv, struct rumpuser_mtx *mtx) {
    osal_cv_wait((struct osal_cv *)cv, (struct osal_mutex *)mtx);
}
int  rumpuser_cv_timedwait(struct rumpuser_cv *cv, struct rumpuser_mtx *mtx,
                           int64_t sec, int64_t nsec) {
    return osal_cv_timedwait((struct osal_cv *)cv, (struct osal_mutex *)mtx,
                             (uint64_t)sec * 1000000000ULL + (uint64_t)nsec);
}
void rumpuser_cv_signal(struct rumpuser_cv *cv)            { osal_cv_signal((struct osal_cv *)cv); }
void rumpuser_cv_broadcast(struct rumpuser_cv *cv)         { osal_cv_broadcast((struct osal_cv *)cv); }
void rumpuser_cv_has_waiters(struct rumpuser_cv *cv, int *waitersp) { (void)cv; *waitersp = 0; }
