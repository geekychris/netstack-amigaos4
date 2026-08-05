/*
 * rumpuser/rumpuser.h — NetBSD rump kernel hypercall interface,
 * as consumed by netstack-amigaos4.
 *
 * This file is a *forward-compatible skeleton*. The canonical
 * definition lives in NetBSD's src/sys/rump/include/rump/rumpuser.h
 * and will be copied verbatim from a pinned NetBSD revision by
 * scripts/import-rump.sh once we start pulling upstream sources.
 *
 * Only the primary function prototypes are listed here — enough
 * for Phase 1 to have something to implement against, and for
 * Phase 2 stubs to declare "here's what rump will expect."
 *
 * Reference: https://netbsd.org/docs/rump/rumpuser.pdf
 */

#ifndef RUMPUSER_H
#define RUMPUSER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward decls used across the interface. Real defs live in the
 * imported NetBSD kernel headers. */
struct lwp;

/* -------- Init / shutdown -------------------------------------- */

struct rumpuser_hyperup;

int rumpuser_init(int version, const struct rumpuser_hyperup *hyp);

/* -------- Memory ----------------------------------------------- */

int  rumpuser_malloc(size_t howmuch, int alignment, void **memp);
void rumpuser_free(void *ptr, size_t size);

int  rumpuser_anonmmap(void *prefaddr, size_t size, int alignbit,
                       int exec, void **memp);
void rumpuser_unmap(void *addr, size_t size);

/* -------- Locking ---------------------------------------------- */

struct rumpuser_mtx;
struct rumpuser_rw;
struct rumpuser_cv;

/* Mutex flags */
#define RUMPUSER_MTX_SPIN   0x01
#define RUMPUSER_MTX_KMUTEX 0x02

void rumpuser_mutex_init(struct rumpuser_mtx **mtx, int flags);
void rumpuser_mutex_enter(struct rumpuser_mtx *mtx);
void rumpuser_mutex_enter_nowrap(struct rumpuser_mtx *mtx);
int  rumpuser_mutex_tryenter(struct rumpuser_mtx *mtx);
void rumpuser_mutex_exit(struct rumpuser_mtx *mtx);
void rumpuser_mutex_destroy(struct rumpuser_mtx *mtx);
void rumpuser_mutex_owner(struct rumpuser_mtx *mtx, struct lwp **lp);

void rumpuser_rw_init(struct rumpuser_rw **rw);
void rumpuser_rw_enter(int type, struct rumpuser_rw *rw);
int  rumpuser_rw_tryenter(int type, struct rumpuser_rw *rw);
int  rumpuser_rw_tryupgrade(struct rumpuser_rw *rw);
void rumpuser_rw_downgrade(struct rumpuser_rw *rw);
void rumpuser_rw_exit(struct rumpuser_rw *rw);
void rumpuser_rw_destroy(struct rumpuser_rw *rw);
void rumpuser_rw_held(int type, struct rumpuser_rw *rw, int *heldp);

void rumpuser_cv_init(struct rumpuser_cv **cv);
void rumpuser_cv_destroy(struct rumpuser_cv *cv);
void rumpuser_cv_wait(struct rumpuser_cv *cv, struct rumpuser_mtx *mtx);
void rumpuser_cv_wait_nowrap(struct rumpuser_cv *cv, struct rumpuser_mtx *mtx);
int  rumpuser_cv_timedwait(struct rumpuser_cv *cv, struct rumpuser_mtx *mtx,
                           int64_t sec, int64_t nsec);
void rumpuser_cv_signal(struct rumpuser_cv *cv);
void rumpuser_cv_broadcast(struct rumpuser_cv *cv);
void rumpuser_cv_has_waiters(struct rumpuser_cv *cv, int *waitersp);

/* -------- Threads / LWPs --------------------------------------- */

int  rumpuser_thread_create(void *(*f)(void *), void *arg,
                            const char *thrname, int mustjoin,
                            int priority, int cpuidx, void **cookiep);
void rumpuser_thread_exit(void) __attribute__((noreturn));
int  rumpuser_thread_join(void *cookie);

struct lwp;
void  rumpuser_curlwpop(int op, struct lwp *l);
struct lwp *rumpuser_curlwp(void);

/* -------- Clock ------------------------------------------------ */

/* Clock IDs matching NetBSD's rumpuser clock indices. */
#define RUMPUSER_CLOCK_RELWALL     0
#define RUMPUSER_CLOCK_ABSMONO     1

int rumpuser_clock_gettime(int enum_rumpclock, int64_t *sec, long *nsec);
int rumpuser_clock_sleep(int enum_rumpclock, int64_t sec, long nsec);

/* -------- I/O -------------------------------------------------- */

/* Simplified — full signature has more flag args. */
int rumpuser_open(const char *name, int mode, int *fdp);
int rumpuser_close(int fd);
int rumpuser_read(int fd, void *buf, size_t nbyte, int64_t offset, size_t *retp);
int rumpuser_write(int fd, const void *buf, size_t nbyte, int64_t offset, size_t *retp);

/* -------- Environment / config -------------------------------- */

int rumpuser_getparam(const char *name, void *buf, size_t buflen);
int rumpuser_putchar(int c);

/* -------- Diagnostics ----------------------------------------- */

void rumpuser_dprintf(const char *fmt, ...);
void rumpuser_exit(int rv) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* RUMPUSER_H */
