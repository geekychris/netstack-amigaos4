/*
 * phase2_engine/fdtable.c — engine-side file-descriptor table.
 *
 * STUB SOCKET LAYER — placeholder until the NetBSD rump kernel
 * is imported. Just an fd allocator; no send/recv semantics
 * yet. Deliberately minimal so that when rump lands, this file
 * either gets deleted (rump manages its own fds via lwp state)
 * or shrinks to a shim mapping our uint32 fds onto rump's.
 *
 * Called only from the engine's dispatch context — no locks
 * needed (single-threaded consumer).
 */

#include "netstack/netstack.h"
#include "netstack/netstack_ipc.h"
#include "netstack/osal.h"

#include <string.h>

#define FDTABLE_MAX 128
#define FD_FIRST    3   /* start above stdin/stdout/stderr, POSIX-ish */

enum fd_kind {
    FD_FREE = 0,
    FD_SOCKET,          /* generic socket, no bind/connect yet */
};

struct fd_entry {
    enum fd_kind  kind;
    int32         domain;
    int32         type;
    int32         proto;
    /* future: bind addr, peer fd, recv queue, ... */
};

static struct fd_entry g_fdtable[FDTABLE_MAX];

static int
fdtable_alloc(enum fd_kind kind, int32 dom, int32 typ, int32 proto)
{
    for (int i = FD_FIRST; i < FDTABLE_MAX; i++) {
        if (g_fdtable[i].kind == FD_FREE) {
            g_fdtable[i].kind   = kind;
            g_fdtable[i].domain = dom;
            g_fdtable[i].type   = typ;
            g_fdtable[i].proto  = proto;
            return i;
        }
    }
    return -1;   /* EMFILE */
}

static int
fdtable_free(int fd)
{
    if (fd < FD_FIRST || fd >= FDTABLE_MAX) return -1;
    if (g_fdtable[fd].kind == FD_FREE) return -1;
    memset(&g_fdtable[fd], 0, sizeof(g_fdtable[fd]));
    return 0;
}

/* -------- Op dispatchers ------------------------------------- */

void
fdtable_dispatch_socket(struct NetstackReq *r)
{
    /* Only stub-accept AF_INET/AF_INET6 for now. Any other
     * family → EAFNOSUPPORT. We just log the request via the
     * fd table — no protocol behavior yet. */
    int fd = fdtable_alloc(FD_SOCKET,
                           r->u.socket.domain,
                           r->u.socket.type,
                           r->u.socket.proto);
    if (fd < 0) {
        r->err            = -24 /* EMFILE */;
        r->u.socket.sock  = -1;
        return;
    }
    r->u.socket.sock = fd;
    r->err           = NETSTACK_OK;
}

void
fdtable_dispatch_close(struct NetstackReq *r)
{
    if (fdtable_free(r->u.close.sock) != 0) {
        r->err = NETSTACK_EBADF;
        return;
    }
    r->err = NETSTACK_OK;
}
