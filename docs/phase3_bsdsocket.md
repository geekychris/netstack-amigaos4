# Phase 3 — bsdsocket.library shim

## Goal

Provide the standard AmigaOS `bsdsocket.library` public interface,
implemented as an RPC bridge to `netstack.process`. Existing OS4
apps (IBrowse, Odyssey, ftp, curl-for-Amiga, ...) work unchanged.

## Non-goals

- Not a source of truth for TCP behavior — every socket op is
  forwarded to the netstack.process.
- Not a replacement for Roadshow's autoconfig / DHCP client (that
  can be a separate `netstack-dhcp` command).
- Not multi-stack — one process, one stack, one library.

## Primary interfaces

### Library exports

Matches the classic AmigaOS `bsdsocket.library` v4 API:

```c
LONG socket(LONG domain, LONG type, LONG protocol);
LONG bind(LONG s, const struct sockaddr *, LONG);
LONG connect(LONG s, const struct sockaddr *, LONG);
LONG send(LONG s, const void *, LONG, LONG);
LONG recv(LONG s, void *, LONG, LONG);
LONG WaitSelect(LONG nfds, fd_set *readfds, fd_set *writefds,
                fd_set *exceptfds, struct timeval *tv, ULONG *sigmask);
LONG CloseSocket(LONG s);
LONG SocketBaseTags(ULONG tag1, ...);
/* ...and the ~50-odd others AmigaOS apps expect */
```

Implemented in OS4 style: `struct Library` header + a
`struct Interface` (`bsdsocket.main` / v1) with function pointers
so callers get the OS4-native calling convention.

### IPC protocol

Every call marshals into a `struct NetstackReq` and PutMsg's to
`netstack.process`'s request port. The library task blocks on its
own reply-port signal (`Wait(1 << reply_port->mp_SigBit)`).

```c
struct NetstackReq {
    struct Message msg;         /* mn_ReplyPort set by caller */
    UWORD          op;          /* NETSTACK_OP_SOCKET, ..._CONNECT, etc. */
    LONG           s;           /* socket fd (in), or return value (out) */
    LONG           err;         /* errno-like (out) */
    /* op-specific fields in a union: */
    union {
        struct { LONG domain, type, protocol; }               socket;
        struct { const struct sockaddr *addr; LONG addrlen; } bind;
        struct { const void *buf; LONG len; LONG flags; }     send;
        /* ... */
    } u;
};
```

### File-descriptor namespace

- Socket fds are integers in a dense namespace owned by
  `netstack.process`.
- Per-task fd tables live in the library, not the engine — this is
  important so that `SocketBaseTags(SBTM_SETVAL(SBTC_SOCKETSIGNAL),
  ...)` and friends work per-task without lock contention.
- `WaitSelect` translates to a poll RPC that returns as soon as any
  fd becomes ready OR the caller-supplied signal mask fires.

## Testing strategy

- `tests/phase3/test_lib_open.c`: OpenLibrary + GetInterface,
  verify version.
- `tests/phase3/test_udp_echo.c`: open UDP socket, send-to-self,
  recv, verify bytes match. Purely bounces through the engine's
  loopback interface (Phase 2).
- `tests/phase3/test_wget.sh`: run stock `wget` against a known
  HTTP server, compare response byte-for-byte with a Roadshow run.
  Requires Phase 4/5 for the actual wire; earlier we can point at
  the loopback and a tiny in-process HTTP responder.

## Known-hard bits

- **`errno` per task.** BSD apps read `errno` after a failed call.
  Store it in `SocketBase->PerTask->errno` addressable via a
  library call `int *__error(void)` — matches how newlib and OS4
  gcc's compat layer expect to find it.
- **Signal-based wakeups.** `WaitSelect` semantics require that
  the calling task can be woken by *either* socket readiness *or*
  a caller-supplied signal mask (`SIGBREAKF_CTRL_C` etc.). RPC
  reply-port signal must interact with `SetSignal` correctly.
- **`struct sockaddr` layout.** Some AmigaOS apps use the historic
  4.3BSD layout (`sin_len` absent); the rump kernel expects
  4.4BSD (`sin_len` present). Shim on the way in and out.
- **`select()` fd_set size.** Historic AmigaOS bsdsocket allowed
  small fd_sets (32 bits). Modern apps expect at least 1024.
  Make the library configurable via `SocketBaseTags` and pass the
  effective size to the engine.

## Current status

**Stub.** `struct Library` + `struct Interface` skeleton compiles;
every function returns `-1` / sets `errno` to `ENOSYS`.
