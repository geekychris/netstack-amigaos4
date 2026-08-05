# Phase 2 — Stack engine (netstack.process)

## Goal

Boot a NetBSD rump kernel inside a dedicated AmigaOS 4 process,
initialize the TCP/IP subsystems, and demonstrate an internal
loopback (`127.0.0.1`) round-trip. No `bsdsocket.library` yet; no
external drivers yet.

## Non-goals

- No user-space socket API in this phase (Phase 3).
- No external NICs (Phase 4/5).
- No configuration UI beyond hardcoded defaults + a couple of
  ENV: variables.

## Primary interfaces

### Startup

```c
int netstack_engine_start(struct NetstackConfig *cfg);
void netstack_engine_stop(void);
```

`netstack_engine_start()` is called by the outer wrapper (either a
CLI `netstack` command or a Kickstart-time module in a later
version). It:

1. Creates the `netstack.process` at priority
   `NETSTACK_ENGINE_PRIORITY` (default 5, tunable via
   `ENV:NETSTACK/PRIORITY`).
2. Waits (via `SIGF_NETSTACK_READY`) for the process to signal
   that rump init completed successfully.
3. Returns 0 on success; non-zero on any init failure (rump panic,
   OSAL missing, out of memory).

### Process main

```c
static void netstack_process_main(void);   /* runs inside the new process */
```

1. `rump_init()` — brings up the rump kernel. Returns 0 if OK.
2. `netstack_init_ifaces()` — creates the loopback interface,
   sets `127.0.0.1/8`, brings it up.
3. `netstack_init_sysctl()` — publishes tuning knobs.
4. Signal `SIGF_NETSTACK_READY` back to the launcher.
5. Enter the event loop: `IExec->Wait()` on a request-port signal;
   dispatch incoming socket-op messages.

### Event loop

```c
while (!engine_shutdown) {
    ULONG sigs = IExec->Wait(request_port_sig | shutdown_sig);
    if (sigs & request_port_sig) drain_requests(request_port);
    if (sigs & shutdown_sig)     break;
}
```

Requests are `struct NetstackReq` messages sent by Phase 3's
bsdsocket.library. Each request identifies an operation (socket,
bind, connect, send, recv, close, poll) and a reply port.

## Testing strategy

- `tests/phase2/test_boot.c`: launches the engine, waits for READY,
  cleanly shuts it down. No packet work.
- `tests/phase2/test_lo_ping.c`: after boot, opens an internal
  socket, sends a UDP packet to `127.0.0.1:9999`, verifies receive.
  This exercises `rump_sys_socket()`, `rump_sys_sendto()`, etc.,
  entirely inside the process — no `bsdsocket.library` needed.
- On-guest smoke: `DH1:netstack-boot` → prints "engine up in Nms"
  and exits.

## Known-hard bits

- **Ordering rump init vs OSAL init.** `rump_init()` immediately
  calls back into OSAL for memory + mutexes + threads. If the OSAL
  isn't wired up before that call, you get a crash inside rump.
  Solved by having the launcher construct the entire OSAL context
  synchronously first, then call `rump_init()`.
- **rump's assumed pageout thread.** Rump kernels don't do paging,
  but the mbuf and kmem allocators keep a background thread alive
  for accounting. Must not accidentally stop it.
- **Symbol collisions.** rump exports thousands of BSD kernel
  symbols. Some collide with newlib/AmigaOS names (e.g., `open`,
  `read`, `time`). Wrap rump in a namespace with linker aliases
  or a `librump_bsd.a` static-link trick. See vendor import notes.

## Current status

**Stub.** `netstack_engine_start()` returns `ENOSYS`. Main loop
skeleton present but doesn't wait on anything real.
