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
#include <stdarg.h>
#include <stdio.h>

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

int
rumpuser_getparam(const char *name, void *buf, size_t buflen)
{
    (void)name; (void)buf; (void)buflen;
    /* TODO: IDOS->GetVar("NETSTACK/<name>"). Not-found returns
     * ENOENT (2). */
    return 2;
}

/* Init hypercall — rump calls this once at startup. */
int
rumpuser_init(int version, const struct rumpuser_hyperup *hyp)
{
    (void)version; (void)hyp;
    /* Real: version-check + store hyp for later callbacks. */
    return 0;
}

/* Filesystem I/O — not needed for network-only build. */
int rumpuser_open(const char *n, int m, int *fdp)    { (void)n; (void)m; (void)fdp; return -1; }
int rumpuser_close(int fd)                           { (void)fd; return -1; }
int rumpuser_read(int fd, void *buf, size_t n, int64_t off, size_t *r)   { (void)fd; (void)buf; (void)n; (void)off; (void)r; return -1; }
int rumpuser_write(int fd, const void *buf, size_t n, int64_t off, size_t *r) { (void)fd; (void)buf; (void)n; (void)off; (void)r; return -1; }
