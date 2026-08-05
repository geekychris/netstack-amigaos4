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

struct osal_cv {
    struct SignalSemaphore  guard;
    LONG                    waiters;
    /* TODO: proper broadcast via MsgPort per waiter; today only
     * signal-one behavior. */
    struct MsgPort         *port;
};

struct osal_cv *
osal_cv_new(void)
{
    struct osal_cv *cv = osal_malloc(sizeof(*cv), 16);
    if (!cv) return NULL;
    IExec->InitSemaphore(&cv->guard);
    cv->port = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);
    if (!cv->port) { osal_free(cv); return NULL; }
    return cv;
}

void
osal_cv_free(struct osal_cv *cv)
{
    if (!cv) return;
    if (cv->port) IExec->FreeSysObject(ASOT_PORT, cv->port);
    osal_free(cv);
}

void
osal_cv_wait(struct osal_cv *cv, struct osal_mutex *mtx)
{
    (void)cv; (void)mtx;
    osal_panic("osal_cv_wait: not implemented");
}

int
osal_cv_timedwait(struct osal_cv *cv, struct osal_mutex *mtx, uint64_t timeout_ns)
{
    (void)cv; (void)mtx; (void)timeout_ns;
    osal_panic("osal_cv_timedwait: not implemented");
    return -1;
}

void osal_cv_signal(struct osal_cv *cv)    { (void)cv; }
void osal_cv_broadcast(struct osal_cv *cv) { (void)cv; }

/* -------- rump hypercall shims --------------------------------- */

void rumpuser_mutex_init(struct rumpuser_mtx **mtx, int flags) {
    (void)flags;
    *mtx = (struct rumpuser_mtx *)osal_mutex_new();
}
void rumpuser_mutex_enter(struct rumpuser_mtx *mtx)  { osal_mutex_lock((struct osal_mutex *)mtx); }
void rumpuser_mutex_enter_nowrap(struct rumpuser_mtx *mtx) { osal_mutex_lock((struct osal_mutex *)mtx); }
int  rumpuser_mutex_tryenter(struct rumpuser_mtx *mtx) { return osal_mutex_trylock((struct osal_mutex *)mtx); }
void rumpuser_mutex_exit(struct rumpuser_mtx *mtx)   { osal_mutex_unlock((struct osal_mutex *)mtx); }
void rumpuser_mutex_destroy(struct rumpuser_mtx *mtx){ osal_mutex_free((struct osal_mutex *)mtx); }
void rumpuser_mutex_owner(struct rumpuser_mtx *mtx, struct lwp **lp) { (void)mtx; *lp = NULL; }

void rumpuser_rw_init(struct rumpuser_rw **rw)             { *rw = (struct rumpuser_rw *)osal_rwlock_new(); }
void rumpuser_rw_enter(int type, struct rumpuser_rw *rw)   { (void)type; osal_rwlock_wlock((struct osal_rwlock *)rw); }
int  rumpuser_rw_tryenter(int t, struct rumpuser_rw *rw)   { (void)t; (void)rw; return 0; }
int  rumpuser_rw_tryupgrade(struct rumpuser_rw *rw)        { (void)rw; return 0; }
void rumpuser_rw_downgrade(struct rumpuser_rw *rw)         { (void)rw; }
void rumpuser_rw_exit(struct rumpuser_rw *rw)              { osal_rwlock_unlock((struct osal_rwlock *)rw); }
void rumpuser_rw_destroy(struct rumpuser_rw *rw)           { osal_rwlock_free((struct osal_rwlock *)rw); }
void rumpuser_rw_held(int t, struct rumpuser_rw *rw, int *heldp) { (void)t; (void)rw; *heldp = 0; }

void rumpuser_cv_init(struct rumpuser_cv **cv)             { *cv = (struct rumpuser_cv *)osal_cv_new(); }
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
