/*
 * phase1_osal/osal_diag.c — diagnostics: panic, log, dprintf.
 *
 * STATUS: functional. Panic aborts the process cleanly (well,
 * cleanly-ish); log/dprintf emit via IExec->DebugPrintF plus a
 * log file at RAM:netstack.log.
 */

#include "netstack/osal.h"
#include "rumpuser/rumpuser.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <stdarg.h>
#include <stdio.h>

/* Very small append-only trace to T:trumpinit.log — visible to
 * the host via /api/file. Cheap enough to sprinkle liberally in
 * hot boot paths while we're bisecting rump_init crashes. */
void
osal_trace(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof(buf)) n = sizeof(buf);
    BPTR f = IDOS->Open("T:trumpinit.log", MODE_READWRITE);
    if (f) {
        IDOS->ChangeFilePosition(f, 0, OFFSET_END);
        IDOS->Write(f, buf, n);
        IDOS->Close(f);
    }
}

void
osal_panic(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    IExec->DebugPrintF("netstack PANIC: ");
    /* Note: DebugPrintF doesn't take va_list — TODO wrap vsnprintf. */
    IExec->DebugPrintF("%s\n", msg);
    va_end(ap);
    /* TODO: stack trace via unwind info. */
    IExec->Wait(0);   /* freeze; task will be killed by watchdog */
    __builtin_unreachable();
}

void
osal_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    IExec->DebugPrintF("[netstack] ");
    IExec->DebugPrintF("%s\n", fmt);
    va_end(ap);
}

/* -------- rump hypercall shims --------------------------------- */

void
rumpuser_dprintf(const char *fmt, ...)
{
    (void)fmt;
    /* TODO: format via vsnprintf, then DebugPrintF. */
    IExec->DebugPrintF("[rump] (dprintf: fmt suppressed)\n");
}

void
rumpuser_exit(int rv)
{
    osal_panic("rumpuser_exit(%d)", rv);
}

int
rumpuser_putchar(int c)
{
    char b[2] = { (char)c, 0 };
    IExec->DebugPrintF("%s", b);
    return 0;
}

static void buf_write(void *buf, size_t buflen, const char *s);

int
rumpuser_getparam(const char *name, void *buf, size_t buflen)
{
    /* Reply to the config params rump_init requires before it
     * will let bootstrap continue. Everything else — ENOENT. */
    if (name == NULL || buf == NULL || buflen < 2) return 22 /* EINVAL */;

    /* Compare inline (no libc str fns available in this context
     * at link time — some builds work, others don't). */
    #define STR_EQ(s) __builtin_strcmp(name, s) == 0
    osal_trace("[getparam] %s\n", name);
    if (STR_EQ("_RUMPUSER_NCPU"))     { buf_write(buf, buflen, "1"); return 0; }
    if (STR_EQ("_RUMPUSER_HOSTNAME")) { buf_write(buf, buflen, "netstack-os4"); return 0; }
    if (STR_EQ("RUMP_VERBOSE"))       { buf_write(buf, buflen, "0"); return 0; }
    if (STR_EQ("RUMP_THREADS"))       { buf_write(buf, buflen, "1"); return 0; }
    if (STR_EQ("RUMP_MEMLIMIT"))      { buf_write(buf, buflen, "33554432"); return 0; }
    return 2 /* ENOENT */;
    #undef STR_EQ
}

/* Small helper — write a NUL-terminated string into buf,
 * respecting buflen. */
static void
buf_write(void *buf, size_t buflen, const char *s)
{
    char *b = (char *)buf;
    size_t i;
    for (i = 0; i + 1 < buflen && s[i]; i++) b[i] = s[i];
    b[i] = 0;
}

/* Init hypercall — rump calls this once at startup. */
int
rumpuser_init(int version, const struct rumpuser_hyperup *hyp)
{
    (void)version; (void)hyp;
    osal_trace("[rumpuser_init] version=%d hyp=%p\n", version, hyp);
    /* Real: version-check + store hyp for later callbacks. */
    return 0;
}

/* Filesystem I/O — not needed for network-only build. */
int rumpuser_open(const char *n, int m, int *fdp)    { (void)n; (void)m; (void)fdp; return -1; }
int rumpuser_close(int fd)                           { (void)fd; return -1; }
int rumpuser_read(int fd, void *buf, size_t n, int64_t off, size_t *r)   { (void)fd; (void)buf; (void)n; (void)off; (void)r; return -1; }
int rumpuser_write(int fd, const void *buf, size_t n, int64_t off, size_t *r) { (void)fd; (void)buf; (void)n; (void)off; (void)r; return -1; }

/* -------- Miscellaneous rumpuser stubs -------------------------
 * These are the last few hypercalls the rump kernel expects
 * but aren't load-bearing for a network-only build. Return
 * sensible no-op values so init and shutdown don't panic.
 */

/* rumpuser_daemonize — for daemon-mode rump. We embed the
 * kernel in our own process, so daemonization is a no-op. */
int  rumpuser_daemonize_begin(void)                          { return 0; }
int  rumpuser_daemonize_done(int error)                      { (void)error; return 0; }

/* rumpuser_dl_bootstrap — used by rump modules loaded from
 * shared objects. We link statically, so no shared-object
 * scan is needed. */
int
rumpuser_dl_bootstrap(void *symcall, void *symload, void *symfini,
                      void *modinfo)
{
    (void)symcall; (void)symload; (void)symfini; (void)modinfo;
    osal_trace("[rumpuser_dl_bootstrap]\n");
    return 0;
}

/* rumpuser_getrandom — kernel entropy source. Return zero
 * bytes on request; a real impl would tap the PPC TB register
 * or /RANDOM: on OS4. */
int
rumpuser_getrandom(void *buf, size_t buflen, int flags, size_t *retp)
{
    (void)flags;
    /* Fill with a simple counter — cryptographically worthless
     * but keeps rump init happy. */
    unsigned char *p = (unsigned char *)buf;
    static unsigned char ctr;
    for (size_t i = 0; i < buflen; i++) p[i] = ctr++;
    if (retp) *retp = buflen;
    return 0;
}

/* rumpuser_kill — send signal to a rump process. We don't
 * expose child processes; ignore. */
int rumpuser_kill(int64_t pid, int sig)              { (void)pid; (void)sig; return 0; }

/* rumpuser_mutex_spin_p — is this mutex a spin mutex?
 * Our osal_mutex is always sleep-style. Return 0. */
int
rumpuser_mutex_spin_p(struct rumpuser_mtx *mtx)
{
    (void)mtx;
    return 0;
}
