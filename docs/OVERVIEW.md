# netstack-amigaos4 — architecture overview

## Component picture

```
                       ┌─────────────────────────────────────────┐
                       │            User-space app               │
                       │  (IBrowse, Odyssey, curl, custom OS4)   │
                       └───────────────┬─────────────────────────┘
                                       │
                    POSIX-ish socket calls (bind, connect, send, recv)
                                       │
                        ┌──────────────▼──────────────┐
                        │  bsdsocket.library          │ ── Phase 3
                        │  (Library + Interface       │
                        │   vector table on OS4)      │
                        └──────────────┬──────────────┘
                                       │
                          message-port RPC (IPC)
                                       │
     ┌──────────────────┐   ┌─────────▼──────────┐   ┌────────────────┐
     │ netstack.library │◄─►│  netstack.process  │◄─►│ SYSCTL config  │
     │ (native zero-    │   │  (dedicated OS4    │   │ (command-line  │
     │  copy API)       │   │   process, pri N)  │   │  utility)      │
     └──────────────────┘   └────────┬───────────┘   └────────────────┘
                                     │  Phase 2
              ┌──────────────────────┼──────────────────────┐
              │                      │                      │
              ▼                      ▼                      ▼
     ┌────────────────┐    ┌─────────────────┐   ┌──────────────────┐
     │  BSD sockets   │    │  BSD TCP/IP/    │   │  BSD routing +   │
     │  (imported     │    │  UDP/ICMP       │   │  ARP + neighbor  │
     │   from NetBSD  │    │  (imported)     │   │  discovery       │
     │   rump kernel) │    │                 │   │                  │
     └────────────────┘    └─────────────────┘   └──────────────────┘
                                     │
                          rump kernel hypercalls
                                     │
     ┌──────────────────────────────▼──────────────────────────────┐
     │              OS Adaptation Layer (OSAL) — Phase 1            │
     │   malloc/free  mutex/rwlock  kthread  callout  cv  copyin    │
     │        ↓            ↓           ↓         ↓       ↓     ↓    │
     │  AllocVecTags  ASO_MUTEX  CreateTask  timer.dev  Wait  memcpy│
     └──────────────────────────────┬──────────────────────────────┘
                                     │
              ┌──────────────────────┴─────────────────────┐
              │                                            │
              ▼                                            ▼
     ┌────────────────┐                          ┌──────────────────┐
     │ NetDev / SANA- │                          │  SANA-II bridge  │
     │ III interface  │   ── Phase 4             │  (Phase 5)       │
     │ (zero-copy,    │                          │  wraps existing  │
     │  ring-based,   │                          │  legacy .device  │
     │  cap-negoti-   │                          │  drivers so they │
     │  ated)         │                          │  work unchanged  │
     └───────┬────────┘                          └────────┬─────────┘
             │                                            │
     ┌───────▼────────┐                          ┌────────▼─────────┐
     │  New drivers   │                          │  Legacy SANA-II  │
     │  (e1000e, re,  │                          │  drivers (virtnet│
     │  ...)          │                          │  virte1000, etc) │
     └────────────────┘                          └──────────────────┘
```

## Boundaries — who owns what

| Layer                       | Phase | Ownership              | State  |
|-----------------------------|-------|------------------------|--------|
| `bsdsocket.library`         | 3     | This repo              | Stub   |
| `netstack.library` (native) | 3/5   | This repo              | Stub   |
| netstack.process (engine)   | 2     | This repo (thin wrap)  | Stub   |
| BSD TCP/IP/socket sources   | 2     | Imported (NetBSD rump) | Not yet imported |
| OSAL (rumpuser_* impls)     | 1     | This repo              | Stub   |
| `NetDev` / SANA-III interface | 4   | This repo              | Stub   |
| Reference NetDev driver     | 4     | This repo              | Stub   |
| SANA-II bridge              | 5     | This repo              | Stub   |
| Existing SANA-II drivers    | —     | Unchanged (virtnet,    | Works today (via SANA-II bridge in Phase 5) |
|                             |       |  virte1000, loopback)  |        |

## Process model

- **`netstack.process`** — a single high-priority AmigaOS 4 process
  that owns all rump-kernel state. All packet processing happens
  here. Multiple caller tasks send message-port requests in; results
  come back on caller-supplied reply ports.
- **Caller tasks** — user-space apps talk to `bsdsocket.library` in
  their own task context; the library marshals the call and PutMsg's
  it to netstack.process's request port. Task blocks on its own
  reply-port signal until netstack.process replies.
- **Driver tasks** — each network driver (either NetDev or SANA-II
  via bridge) runs its own task, kicked by IRQ or by device-manager
  BeginIO callbacks. Drivers push received frames to netstack.process
  via a per-driver mbuf-queue + signal.

## Key non-obvious decisions

1. **No IExec BeginIO/DoIO on the netstack hot path.** BSD-kernel code
   was never designed for the OS4 device-manager RPC pattern. All
   driver→stack and stack→driver hand-offs are direct function calls
   (via NetDev vectors) or shared-memory queues (via SANA-II bridge).
2. **All rump kernel memory comes from `MEMF_SHARED` pools.** BSD
   assumes symmetric memory model; `MEMF_PRIVATE` (fast, CPU-local)
   would break driver DMA and cross-task pointer passing.
3. **Timers use `timer.device`, not spinning task loops.** The BSD
   `callout` API maps cleanly to `TR_ADDREQUEST`.
4. **No preemption inside the netstack.process.** BSD kernel code
   assumes cooperative scheduling within a "kernel" context.
   `Forbid()`/`Permit()` brackets around critical sections; the
   process runs at priority chosen so drivers can preempt but user
   tasks cannot.

## What isn't drawn above

- The signal-forwarding path for `select()` / `poll()` /
  wakeup-on-socket: Phase 3 detail.
- The SYSCTL / configuration path: Phase 2/3 detail.
- The routing socket (`AF_ROUTE`): Phase 2, used by DHCP clients etc.
