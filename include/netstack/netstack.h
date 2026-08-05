/*
 * netstack/netstack.h — public API for the netstack.library
 * native (non-bsdsocket) interface.
 *
 * This is the low-latency, zero-copy API modern OS4 apps should
 * link against directly, bypassing the bsdsocket.library POSIX
 * shim. Provides:
 *
 *   - Direct socket ops that avoid the RPC marshalling cost
 *     (calls happen in the caller's task, syscall args passed
 *     by pointer to shared memory).
 *   - mbuf-aware send/recv that can hand ownership of an
 *     allocated buffer to/from the stack without a copy.
 *   - Native polling that integrates with OS4 signals.
 *
 * STATUS: header only. src/phase3_bsdsocket/netstack_library.c
 * has stubs.
 */

#ifndef NETSTACK_NETSTACK_H
#define NETSTACK_NETSTACK_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/interfaces.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Library / Interface identification. The library exports a
 * single "main" interface at v1; future major versions bump
 * this number and add a parallel interface, never break "main".
 */
#define NETSTACK_LIBRARY_NAME     "netstack.library"
#define NETSTACK_INTERFACE_NAME   "main"
#define NETSTACK_INTERFACE_VERSION 1

/* -------- Interface vector table ------------------------------- */

struct NetstackIFace;

/* Lifecycle */
typedef LONG (*NetstackObtainFn)(struct NetstackIFace *);
typedef LONG (*NetstackReleaseFn)(struct NetstackIFace *);

/* Native socket API */
typedef LONG (*NetstackSocketFn)(struct NetstackIFace *,
                                 LONG domain, LONG type, LONG proto);
typedef LONG (*NetstackCloseFn)(struct NetstackIFace *, LONG s);
typedef LONG (*NetstackBindFn)(struct NetstackIFace *, LONG s,
                                const void *addr, LONG addrlen);
typedef LONG (*NetstackListenFn)(struct NetstackIFace *, LONG s, LONG backlog);
typedef LONG (*NetstackAcceptFn)(struct NetstackIFace *, LONG s,
                                  void *addr, LONG *addrlen);
typedef LONG (*NetstackConnectFn)(struct NetstackIFace *, LONG s,
                                   const void *addr, LONG addrlen);

/* Zero-copy send/recv — hand ownership of pre-populated buffers.
 * On send, buffer ownership transfers to the stack (freed when
 * the L2 driver is done with it). On recv, ownership transfers
 * to the caller (must be freed by netstack_freebuf). */
struct NetstackBuf;
typedef LONG (*NetstackSendBufFn)(struct NetstackIFace *, LONG s,
                                   struct NetstackBuf *, LONG flags);
typedef LONG (*NetstackRecvBufFn)(struct NetstackIFace *, LONG s,
                                   struct NetstackBuf **out, LONG flags);
typedef void (*NetstackFreeBufFn)(struct NetstackIFace *, struct NetstackBuf *);

/* Signal integration — get a per-socket signal bit the caller
 * can Wait() on. The bit is signalled whenever the socket
 * becomes readable / writable per the mask supplied. */
#define NETSTACK_SIG_READABLE   (1u << 0)
#define NETSTACK_SIG_WRITABLE   (1u << 1)
typedef LONG (*NetstackGetSignalFn)(struct NetstackIFace *, LONG s,
                                     ULONG mask, ULONG *sigbit);
typedef LONG (*NetstackReleaseSignalFn)(struct NetstackIFace *, LONG s);

/* Config / sysctl access */
typedef LONG (*NetstackSysctlFn)(struct NetstackIFace *,
                                  const char *name,
                                  void *oldval, size_t *oldsize,
                                  const void *newval, size_t newsize);

struct NetstackIFace {
    struct InterfaceData Data;

    NetstackObtainFn         Obtain;
    NetstackReleaseFn        Release;
    APTR                     reserved0;
    APTR                     reserved1;

    NetstackSocketFn         Socket;
    NetstackCloseFn          CloseSocket;
    NetstackBindFn           Bind;
    NetstackListenFn         Listen;
    NetstackAcceptFn         Accept;
    NetstackConnectFn        Connect;

    NetstackSendBufFn        SendBuf;
    NetstackRecvBufFn        RecvBuf;
    NetstackFreeBufFn        FreeBuf;

    NetstackGetSignalFn      GetSignal;
    NetstackReleaseSignalFn  ReleaseSignal;

    NetstackSysctlFn         Sysctl;
};

/* -------- NetstackBuf ------------------------------------------ */

/*
 * Wraps a data buffer with metadata needed by the stack. On send,
 * the caller allocates via netstack_allocbuf(), fills `data[0..len]`,
 * and hands it off — the stack may zero-copy it into an mbuf.
 * On recv, the stack allocates and the caller frees via FreeBuf.
 */
struct NetstackBuf {
    APTR    data;
    uint32  len;
    uint32  size;
    ULONG   flags;
    APTR    priv;    /* stack-private */
};

/* -------- Engine control --------------------------------------- */

/*
 * Called by external tooling (netstack.command, or the Roadshow
 * replacement wrapper) to start/stop the engine. The library is
 * usable only when the engine is running.
 */
struct NetstackConfig {
    ULONG  priority;              /* process priority, default 5 */
    ULONG  mbuf_pool_kb;          /* mbuf pool size, default 4096 */
    ULONG  socket_max;            /* max concurrent sockets, default 1024 */
    STRPTR default_iface;         /* NetDev name to bring up at start */
};

LONG NetstackEngine_Start(struct NetstackConfig *cfg);
LONG NetstackEngine_Stop(void);
BOOL NetstackEngine_IsRunning(void);

#ifdef __cplusplus
}
#endif

#endif /* NETSTACK_NETSTACK_H */
