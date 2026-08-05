/*
 * phase3_bsdsocket/bsdsocket_library.c — bsdsocket.library skeleton.
 *
 * STATUS: skeleton. Compiles as a placeholder .library with the
 * standard OS4 Library + Interface machinery, but every socket
 * call returns -1 / ENOSYS.
 *
 * Follows the same skeleton pattern that virtnet.device /
 * loopback.device use for .device drivers — Init/Open/Close/
 * Expunge boilerplate plus a "main" Interface vector table.
 */

#include "netstack/netstack.h"
#include "netstack/osal.h"

#include <exec/interfaces.h>
#include <exec/libraries.h>
#include <exec/resident.h>
#include <proto/exec.h>

#define LIBNAME "bsdsocket.library"

/* -------- Library base ---------------------------------------- */

struct BsdsocketBase {
    struct Library         lib;
    struct ExecIFace      *IExec;
    /* TODO: per-task fd table, socket-count, engine handle. */
};

/* -------- Interface stubs ------------------------------------- */

/* Full v4 bsdsocket API surface, all returning -1 for now. */

static LONG b_socket(struct Interface *Self, LONG d, LONG t, LONG p)
{ (void)Self; (void)d; (void)t; (void)p; return -1; }

static LONG b_bind(struct Interface *Self, LONG s, const void *a, LONG l)
{ (void)Self; (void)s; (void)a; (void)l; return -1; }

static LONG b_connect(struct Interface *Self, LONG s, const void *a, LONG l)
{ (void)Self; (void)s; (void)a; (void)l; return -1; }

static LONG b_listen(struct Interface *Self, LONG s, LONG b)
{ (void)Self; (void)s; (void)b; return -1; }

static LONG b_accept(struct Interface *Self, LONG s, void *a, LONG *l)
{ (void)Self; (void)s; (void)a; (void)l; return -1; }

static LONG b_send(struct Interface *Self, LONG s, const void *b, LONG l, LONG f)
{ (void)Self; (void)s; (void)b; (void)l; (void)f; return -1; }

static LONG b_recv(struct Interface *Self, LONG s, void *b, LONG l, LONG f)
{ (void)Self; (void)s; (void)b; (void)l; (void)f; return -1; }

static LONG b_close(struct Interface *Self, LONG s)
{ (void)Self; (void)s; return -1; }

/* -------- OS4 driver boilerplate ------------------------------ */

/* TODO: wire up Init/Open/Close/Expunge with resident tag,
 * DeviceManagerInterface vector table, etc. Copy the pattern
 * from sibling loopback.device src/device.c.
 *
 * For now this file compiles as a normal object; not yet a
 * linkable .library.
 */

void
bsdsocket_placeholder(void)
{
    /* Reference the stubs so the linker doesn't drop them. */
    (void)b_socket; (void)b_bind; (void)b_connect; (void)b_listen;
    (void)b_accept; (void)b_send; (void)b_recv; (void)b_close;
}
