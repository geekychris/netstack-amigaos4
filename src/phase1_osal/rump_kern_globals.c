/*
 * phase1_osal/rump_kern_globals.c — kernel globals + panic
 * expected by the imported rump kernel sources.
 *
 * Rump's own librump/rumpkern/ (which we're now compiling
 * against — see docs/rump_build_probe.md) provides real
 * implementations of mutex_init/enter/exit/cv_wait/signal/etc.
 * as thin wrappers over rumpuser_* hypercalls. But those
 * wrappers still reference a handful of kernel globals (hz,
 * panic, etc.) that classically come from the NetBSD kernel
 * proper.
 *
 * This file defines the minimum set so rump_locks.o and
 * friends can link.
 */

#include "netstack/osal.h"

#include <stdarg.h>
#include <stddef.h>

/* -------- hz — kernel clock tick rate --------
 * NetBSD kernel default is 100. Rump uses it for timedwait
 * conversions. Value doesn't have to match anything real on
 * OS4; it's just a scale factor. */
int hz = 100;

/* -------- rump-side flags --------
 * rump.h in the imported source declares these as `int`.
 * rump_threads controls whether rump spawns real threads
 * (yes for us) and lockdebug enables slow lock checking (no).
 */
int rump_threads = 1;
int rump_lockdebug = 0;

/* -------- panic — kernel abort --------
 * NetBSD's panic() is (const char *fmt, ...) and never
 * returns. Our OSAL panic does the equivalent — halts the
 * task with a DebugPrintF marker.
 */
void
panic(const char *fmt, ...)
{
    (void)fmt;
    /* TODO: format the args via a per-task static buffer +
     * vsnprintf. For now just funnel through osal_panic which
     * DebugPrintF's a short message + halts. */
    osal_panic("kernel panic (rump)");
    __builtin_unreachable();
}

/* -------- nullop / nullret — kernel stubs --------
 * Used in vtables and as default handlers throughout the
 * kernel when a subsystem doesn't want to implement a hook.
 */
int
nullop(void *arg)
{
    (void)arg;
    return 0;
}

/* -------- Memory barriers — PPC implementations --------
 * NetBSD's membar_*() family. On PPC 460 (BookE), `sync` is
 * the heavy barrier; `lwsync` is the lightweight variant used
 * by acquire/release. `eieio` orders MMIO but not RAM, so we
 * don't use it here.
 */
void membar_acquire(void)  { __asm__ __volatile__ ("lwsync" ::: "memory"); }
void membar_release(void)  { __asm__ __volatile__ ("lwsync" ::: "memory"); }
void membar_consumer(void) { __asm__ __volatile__ ("lwsync" ::: "memory"); }
void membar_enter(void)    { __asm__ __volatile__ ("sync"   ::: "memory"); }
void membar_producer(void) { __asm__ __volatile__ ("lwsync" ::: "memory"); }
void membar_sync(void)     { __asm__ __volatile__ ("sync"   ::: "memory"); }

/* -------- kpreempt hooks --------
 * Our rump kernel runs on a single AmigaOS Task at a time —
 * NetBSD-style kernel preemption doesn't apply. Return 0
 * (no preemption pending) unconditionally.
 */
int  kpreempt(int where) { (void)where; return 0; }
void kpreempt_disable(void) { }
void kpreempt_enable(void)  { }

/* -------- lockdebug — off for us --------
 * Called from inside mutex/rwlock code when LOCKDEBUG is
 * defined in the build. We build with LOCKDEBUG off (see
 * opt_rumpkernel.h), so these should never fire. Provide
 * a panic-if-called stub in case.
 */
void
lockdebug_abort(void *cookie, void *lock, const char *ops,
                const char *func, const char *msg)
{
    (void)cookie; (void)lock; (void)ops; (void)func; (void)msg;
    osal_panic("lockdebug_abort: unreachable (LOCKDEBUG disabled)");
}
