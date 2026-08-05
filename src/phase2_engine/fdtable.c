/*
 * phase2_engine/fdtable.c — engine-side fd table + stub socket layer.
 *
 * SCAFFOLD — an in-memory socket implementation to exercise the
 * Phase 3 client wrappers and the eventual bsdsocket.library
 * shim end-to-end without a real TCP/IP stack. Replaced when
 * the NetBSD rump kernel is imported.
 *
 * Non-blocking semantics: any op that would block returns
 * NETSTACK_EAGAIN. Caller polls.
 *
 * Called only from the engine's dispatch context, so no locking
 * is needed. When rump lands and dispatch calls actually block
 * inside rump_sys_* — different problem, different design.
 */

#include "netstack/netstack.h"
#include "netstack/netstack_ipc.h"
#include "netstack/osal.h"

#include <string.h>

#define FDTABLE_MAX     128
#define FD_FIRST        3
#define BACKLOG_MAX     4
#define RXBUF_SIZE      4096

enum fd_kind {
    FD_FREE = 0,
    FD_SOCKET_UNBOUND,     /* created, not bound/connected */
    FD_SOCKET_LISTEN,      /* bound + listening */
    FD_SOCKET_CONNECTED,   /* paired with a peer */
};

struct fd_entry {
    enum fd_kind  kind;
    int32         domain;
    int32         type;
    int32         proto;

    /* For LISTEN: */
    uint16        local_port;
    int32         backlog[BACKLOG_MAX];   /* pending peer fds */
    int32         backlog_head;
    int32         backlog_tail;
    int32         backlog_count;

    /* For CONNECTED: */
    int32         peer_fd;
    uint16        peer_port;

    /* rx byte queue: peer sends → we recv here. Simple linear
     * buffer with head/tail; wrap-free (compact on read). */
    uint8         rx[RXBUF_SIZE];
    int32         rx_head;   /* next byte to consume */
    int32         rx_tail;   /* one past last valid byte */
};

static struct fd_entry g_fdtable[FDTABLE_MAX];

/* -------- Helpers -------------------------------------------- */

static int
alloc_fd(void)
{
    for (int i = FD_FIRST; i < FDTABLE_MAX; i++) {
        if (g_fdtable[i].kind == FD_FREE) {
            memset(&g_fdtable[i], 0, sizeof(g_fdtable[i]));
            g_fdtable[i].peer_fd = -1;
            return i;
        }
    }
    return -1;
}

static int
fd_valid(int fd)
{
    return fd >= FD_FIRST && fd < FDTABLE_MAX &&
           g_fdtable[fd].kind != FD_FREE;
}

static int
find_listener(uint16 port)
{
    for (int i = FD_FIRST; i < FDTABLE_MAX; i++) {
        if (g_fdtable[i].kind == FD_SOCKET_LISTEN &&
            g_fdtable[i].local_port == port) {
            return i;
        }
    }
    return -1;
}

static int
port_in_use(uint16 port)
{
    for (int i = FD_FIRST; i < FDTABLE_MAX; i++) {
        if ((g_fdtable[i].kind == FD_SOCKET_LISTEN ||
             g_fdtable[i].kind == FD_SOCKET_CONNECTED) &&
            g_fdtable[i].local_port == port) {
            return 1;
        }
    }
    return 0;
}

/* Compact rx queue: shift live bytes to the head so tail-space
 * grows. Called on send() so peer always has room after we
 * consume. */
static void
rx_compact(struct fd_entry *e)
{
    if (e->rx_head == 0) return;
    int32 live = e->rx_tail - e->rx_head;
    if (live > 0) memmove(e->rx, e->rx + e->rx_head, (size_t)live);
    e->rx_tail = live;
    e->rx_head = 0;
}

/* -------- Dispatchers --------------------------------------- */

void
fdtable_dispatch_socket(struct NetstackReq *r)
{
    int fd = alloc_fd();
    if (fd < 0) {
        r->err = -24 /* EMFILE */;
        r->u.socket.sock = -1;
        return;
    }
    g_fdtable[fd].kind   = FD_SOCKET_UNBOUND;
    g_fdtable[fd].domain = r->u.socket.domain;
    g_fdtable[fd].type   = r->u.socket.type;
    g_fdtable[fd].proto  = r->u.socket.proto;
    r->u.socket.sock = fd;
    r->err = NETSTACK_OK;
}

void
fdtable_dispatch_close(struct NetstackReq *r)
{
    int fd = r->u.close.sock;
    if (!fd_valid(fd)) { r->err = NETSTACK_EBADF; return; }

    /* If we were connected, tell the peer we're gone. Simplest:
     * clear their peer_fd so their next send returns ENOTCONN. */
    if (g_fdtable[fd].kind == FD_SOCKET_CONNECTED) {
        int peer = g_fdtable[fd].peer_fd;
        if (peer >= 0 && fd_valid(peer)) {
            g_fdtable[peer].peer_fd = -1;
        }
    }
    memset(&g_fdtable[fd], 0, sizeof(g_fdtable[fd]));
    g_fdtable[fd].peer_fd = -1;
    r->err = NETSTACK_OK;
}

void
fdtable_dispatch_bind(struct NetstackReq *r)
{
    int fd = r->u.bind.sock;
    if (!fd_valid(fd)) { r->err = NETSTACK_EBADF; return; }
    if (g_fdtable[fd].kind != FD_SOCKET_UNBOUND) {
        r->err = NETSTACK_EINVAL; return;
    }
    if (port_in_use(r->u.bind.port)) {
        r->err = NETSTACK_EADDRINUSE; return;
    }
    g_fdtable[fd].local_port = r->u.bind.port;
    r->err = NETSTACK_OK;
}

void
fdtable_dispatch_listen(struct NetstackReq *r)
{
    int fd = r->u.listen.sock;
    if (!fd_valid(fd)) { r->err = NETSTACK_EBADF; return; }
    if (g_fdtable[fd].kind != FD_SOCKET_UNBOUND ||
        g_fdtable[fd].local_port == 0) {
        r->err = NETSTACK_EINVAL; return;
    }
    g_fdtable[fd].kind = FD_SOCKET_LISTEN;
    g_fdtable[fd].backlog_head = 0;
    g_fdtable[fd].backlog_tail = 0;
    g_fdtable[fd].backlog_count = 0;
    r->err = NETSTACK_OK;
}

void
fdtable_dispatch_connect(struct NetstackReq *r)
{
    int fd = r->u.connect.sock;
    if (!fd_valid(fd)) { r->err = NETSTACK_EBADF; return; }
    if (g_fdtable[fd].kind != FD_SOCKET_UNBOUND) {
        r->err = NETSTACK_EINVAL; return;
    }
    int lfd = find_listener(r->u.connect.port);
    if (lfd < 0) { r->err = NETSTACK_ECONNREFUSED; return; }
    struct fd_entry *lst = &g_fdtable[lfd];
    if (lst->backlog_count >= BACKLOG_MAX) {
        /* Backlog full — non-blocking connect returns EAGAIN. */
        r->err = NETSTACK_EAGAIN; return;
    }
    /* Mark our fd as connected but with peer_fd = -1 (unset until
     * accept). Server-side fd will be minted by accept and paired
     * back at that point. */
    g_fdtable[fd].kind      = FD_SOCKET_CONNECTED;
    g_fdtable[fd].peer_fd   = -1;   /* set by accept */
    g_fdtable[fd].peer_port = r->u.connect.port;
    lst->backlog[lst->backlog_tail] = fd;
    lst->backlog_tail = (lst->backlog_tail + 1) % BACKLOG_MAX;
    lst->backlog_count++;
    r->err = NETSTACK_OK;
}

void
fdtable_dispatch_accept(struct NetstackReq *r)
{
    int fd = r->u.accept.sock;
    if (!fd_valid(fd)) { r->err = NETSTACK_EBADF; return; }
    if (g_fdtable[fd].kind != FD_SOCKET_LISTEN) {
        r->err = NETSTACK_EINVAL; return;
    }
    struct fd_entry *lst = &g_fdtable[fd];
    if (lst->backlog_count == 0) {
        r->err = NETSTACK_EAGAIN;
        r->u.accept.new_sock = -1;
        return;
    }
    int client_fd = lst->backlog[lst->backlog_head];
    lst->backlog_head = (lst->backlog_head + 1) % BACKLOG_MAX;
    lst->backlog_count--;

    int new_fd = alloc_fd();
    if (new_fd < 0) {
        /* Return the pending client to the backlog head so caller
         * can retry — not perfect but keeps invariants. */
        lst->backlog_head = (lst->backlog_head + BACKLOG_MAX - 1) % BACKLOG_MAX;
        lst->backlog[lst->backlog_head] = client_fd;
        lst->backlog_count++;
        r->err = -24 /* EMFILE */;
        r->u.accept.new_sock = -1;
        return;
    }
    /* Pair server-side (new_fd) with client-side (client_fd). */
    g_fdtable[new_fd].kind      = FD_SOCKET_CONNECTED;
    g_fdtable[new_fd].domain    = g_fdtable[fd].domain;
    g_fdtable[new_fd].type      = g_fdtable[fd].type;
    g_fdtable[new_fd].proto     = g_fdtable[fd].proto;
    g_fdtable[new_fd].local_port = g_fdtable[fd].local_port;
    g_fdtable[new_fd].peer_fd   = client_fd;
    g_fdtable[new_fd].peer_port = 0;   /* stub: client's port unknown */
    g_fdtable[client_fd].peer_fd = new_fd;

    r->u.accept.new_sock  = new_fd;
    r->u.accept.peer_port = g_fdtable[fd].local_port;
    r->err = NETSTACK_OK;
}

void
fdtable_dispatch_send(struct NetstackReq *r)
{
    int fd = r->u.send.sock;
    if (!fd_valid(fd)) { r->err = NETSTACK_EBADF; return; }
    if (g_fdtable[fd].kind != FD_SOCKET_CONNECTED ||
        g_fdtable[fd].peer_fd < 0) {
        r->err = NETSTACK_ENOTCONN; return;
    }
    int peer = g_fdtable[fd].peer_fd;
    if (!fd_valid(peer)) { r->err = NETSTACK_ENOTCONN; return; }

    int32 len = r->u.send.len_in;
    if (len < 0 || len > NETSTACK_MAX_PAYLOAD) {
        r->err = NETSTACK_EMSGSIZE; return;
    }

    struct fd_entry *pe = &g_fdtable[peer];
    rx_compact(pe);
    int32 room = RXBUF_SIZE - pe->rx_tail;
    if (room <= 0) {
        r->err = NETSTACK_EAGAIN;
        r->u.send.len_out = 0;
        return;
    }
    int32 to_write = len < room ? len : room;
    memcpy(pe->rx + pe->rx_tail, r->u.send.data, (size_t)to_write);
    pe->rx_tail += to_write;

    r->u.send.len_out = to_write;
    r->err = NETSTACK_OK;
}

void
fdtable_dispatch_recv(struct NetstackReq *r)
{
    int fd = r->u.recv.sock;
    if (!fd_valid(fd)) { r->err = NETSTACK_EBADF; return; }
    if (g_fdtable[fd].kind != FD_SOCKET_CONNECTED) {
        r->err = NETSTACK_ENOTCONN; return;
    }

    struct fd_entry *e = &g_fdtable[fd];
    int32 avail = e->rx_tail - e->rx_head;
    if (avail <= 0) {
        /* If peer has been closed and no data left → EOF (len=0
         * with OK). Otherwise EAGAIN. */
        if (e->peer_fd < 0 || !fd_valid(e->peer_fd)) {
            r->u.recv.len_out = 0;
            r->err = NETSTACK_OK;
            return;
        }
        r->err = NETSTACK_EAGAIN;
        r->u.recv.len_out = 0;
        return;
    }
    int32 want = r->u.recv.len_in;
    if (want < 0) want = 0;
    if (want > NETSTACK_MAX_PAYLOAD) want = NETSTACK_MAX_PAYLOAD;
    int32 to_read = avail < want ? avail : want;
    memcpy(r->u.recv.data, e->rx + e->rx_head, (size_t)to_read);
    e->rx_head += to_read;
    if (e->rx_head == e->rx_tail) { e->rx_head = e->rx_tail = 0; }

    r->u.recv.len_out = to_read;
    r->err = NETSTACK_OK;
}
