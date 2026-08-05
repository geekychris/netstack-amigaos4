/*
 * netstack/osal.h — OS Adaptation Layer public API.
 *
 * Thin wrappers over the ExecSG primitives that Phase 1's
 * rumpuser_* implementations use internally. Exposed publicly so
 * Phase 2-5 code can share the same abstractions without pulling
 * in <proto/exec.h> everywhere.
 *
 * STATUS: header only. Implementations in src/phase1_osal/ are
 * all stubs that panic-not-implemented when called.
 */

#ifndef NETSTACK_OSAL_H
#define NETSTACK_OSAL_H

#include <exec/types.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------- Memory -------------------------------------------------- */

/*
 * osal_malloc — allocate `size` bytes with the given alignment.
 *
 * `align` must be a power of two, or 0 for default (16). Returns
 * NULL on failure. Memory comes from a shared-memory pool suitable
 * for cross-task hand-off; use osal_dma_malloc() if the caller
 * needs DMA-visible pages.
 */
void  *osal_malloc(size_t size, size_t align);
void   osal_free(void *ptr);

void  *osal_dma_malloc(size_t size, size_t align);
void   osal_dma_free(void *ptr);

/* -------- Mutexes ------------------------------------------------- */

/*
 * Recursive mutex. Backed by ASOT_MUTEX on OS4.
 */
struct osal_mutex;

struct osal_mutex *osal_mutex_new(void);
void               osal_mutex_free(struct osal_mutex *);
void               osal_mutex_lock(struct osal_mutex *);
int                osal_mutex_trylock(struct osal_mutex *);   /* 1 = got it */
void               osal_mutex_unlock(struct osal_mutex *);

/* -------- Reader-writer lock ------------------------------------- */

struct osal_rwlock;

struct osal_rwlock *osal_rwlock_new(void);
void                osal_rwlock_free(struct osal_rwlock *);
void                osal_rwlock_rlock(struct osal_rwlock *);
void                osal_rwlock_wlock(struct osal_rwlock *);
void                osal_rwlock_unlock(struct osal_rwlock *);

/* -------- Condition variable ------------------------------------- */

struct osal_cv;

struct osal_cv *osal_cv_new(void);
void            osal_cv_free(struct osal_cv *);
void            osal_cv_wait(struct osal_cv *, struct osal_mutex *);
int             osal_cv_timedwait(struct osal_cv *, struct osal_mutex *,
                                  uint64_t timeout_ns);   /* 0 = signaled, ETIMEDOUT otherwise */
void            osal_cv_signal(struct osal_cv *);
void            osal_cv_broadcast(struct osal_cv *);

/* -------- Threads ------------------------------------------------- */

struct osal_thread;

typedef void (*osal_thread_fn)(void *arg);

int              osal_thread_create(osal_thread_fn fn, void *arg,
                                    const char *name, int priority,
                                    struct osal_thread **out);
int              osal_thread_join(struct osal_thread *);
struct osal_thread *osal_thread_self(void);

/* -------- Timers -------------------------------------------------- */

/* Monotonic clock in nanoseconds since some epoch (implementation-defined,
 * usually netstack init time). */
uint64_t osal_clock_monotonic_ns(void);

/* Sleep the current thread. */
void     osal_sleep_ns(uint64_t ns);

/* One-shot timer. Callback fires from a timer thread, not the caller's
 * context — user must lock if it touches shared state. */
struct osal_timer;
typedef void (*osal_timer_fn)(void *arg);

struct osal_timer *osal_timer_new(osal_timer_fn fn, void *arg);
void               osal_timer_free(struct osal_timer *);
void               osal_timer_schedule(struct osal_timer *, uint64_t delay_ns);
void               osal_timer_cancel(struct osal_timer *);

/* -------- Diagnostics -------------------------------------------- */

void osal_panic(const char *msg, ...) __attribute__((noreturn));
void osal_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* NETSTACK_OSAL_H */
