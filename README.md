# netstack-amigaos4 — modern BSD network stack for AmigaOS 4

**Status: WORK-IN-PROGRESS.** OSAL primitives verified on the OS4
guest; a NetBSD rump kernel subset compiles and archives into a
983 KB `librump.a`; a stub in-memory socket layer runs
end-to-end through a named-port RPC. Rump-side symbol closure
about 82% complete. **Not yet linked to a runnable executable.**
See [Progress](#progress) below for the full checklist.

## What this is

A long-horizon project to bring a modern BSD-derived TCP/IP stack
(NetBSD rump kernel, ~2024+) to AmigaOS 4 as a replacement for
Roadshow / AmiTCP-derived stacks. Delivers:

- A high-performance, actively-maintained TCP/IPv4/IPv6 stack running
  as an OS4 process.
- A compatibility `bsdsocket.library` so existing apps (IBrowse,
  Odyssey, ftp, etc.) work unchanged.
- A modern, zero-copy driver interface (working title: **NetDev** /
  SANA-III) alongside a legacy SANA-II bridge so existing drivers
  keep working during the transition.

See [`docs/OVERVIEW.md`](docs/OVERVIEW.md) for the architecture
picture and [`docs/roadmap.md`](docs/roadmap.md) for realistic time
estimates (spoiler: multiple quarters of full-time work).

## Reality check

Porting a modern BSD network stack is a multi-quarter effort even
with NetBSD rump kernels doing most of the isolation work. Do not
confuse the concrete progress below with "stack working" — those
are separated by 3-6 months of steady kernel-porting work.

## Choice of upstream

**NetBSD rump kernel**, not FreeBSD extraction. NetBSD's rump
framework is purpose-built for hosting kernel subsystems outside
their native kernel: `sys/net`, `sys/netinet`, `sys/netinet6` are
already isolated behind a ~50-function `rumpuser_*` hypercall
API. The OSAL surface area is a fraction of what a raw FreeBSD
extraction would require.

## Progress

Legend: `[x]` complete + verified · `[~]` partial · `[ ]` not started.
Verified = compiled and run on the OS4 guest, output captured in
commit messages.

### Repo scaffold
- [x] **5-phase directory layout** — `src/phase{1..5}/`, `include/{netstack,rumpuser}/`, `docs/`, `tests/`
- [x] **Docker cross-compile wrapper** — `scripts/build.sh` uses `walkero/amigagccondocker:os4-gcc11-arm64`
- [x] **Per-phase docs** — `OVERVIEW.md`, `roadmap.md`, `phase1..5_*.md`, `rump_build_probe.md`, `rump_compile_report.md`

### Phase 1 — OSAL (OS Adaptation Layer)
- [x] **Memory** — `osal_malloc/free`, DMA variants (via `AllocVecTags(MEMF_SHARED)`)
- [x] **Mutex** — `osal_mutex_*` on `SignalSemaphore`; verified 8-way contention
- [x] **Reader-writer lock** — `osal_rwlock_*` on shared-mode semaphore
- [x] **Condition variables** — `osal_cv_*` with per-cv waiter list + `SIGF_SINGLE`; producer/consumer test green
- [x] **Threads** — `osal_thread_create/join/self` on `CreateTaskTags` + trampoline; join via poll-on-done (Signal path was flaky, poll is reliable)
- [x] **Sleep** — `osal_sleep_ns` via `timer.device` (per-task IORequest table, races were the initial bug)
- [x] **Clock** — `osal_clock_monotonic_ns` via PPC `mftb`; proportional but off ~10× (TB frequency wrong)
- [x] **Diagnostics** — `osal_panic`, `osal_log`, `rumpuser_dprintf`, `rumpuser_exit`
- [x] **PPC atomics** — `atomic_cas_32/uint/ptr`, add/or/and/inc/dec/swap variants, `bswap32/64` via `lwarx`/`stwcx.`
- [x] **PPC memory barriers** — `membar_acquire/release/consumer/enter/producer/sync` via `lwsync`/`sync`
- [x] **Kernel globals for rump** — `hz`, `rump_threads`, `rump_lockdebug`, `panic`, `nullop`, `kpreempt*`, `lockdebug_abort`, autogen stand-ins
- [x] **All 47 `rumpuser_*` hypercalls** — full stubs; memory/lock/thread families call real Phase 1 code
- [ ] **`osal_timer_new/schedule/cancel`** — still stub; needs a background timer-service task
- [ ] **TB frequency calibration** — clock reports ~10× real; needs runtime measure or switch to `timer.device` for wall clock
- [ ] **Real rump syscall generator** — `rump_syscalls.c` / `rumpkern_if_wrappers.c` from `syscalls.master`

### Phase 2 — Engine (`netstack.process`)
- [x] **Process bring-up** — spawn at pri 5, publish named MsgPort `"netstack.request"`, READY/STOPPED signal handshake
- [x] **Dispatch loop** — Wait on port + CTRL_C; ReplyMsg per request; graceful `NETSTACK_OP_SHUTDOWN`
- [x] **RPC round-trip verified** — `NETSTACK_OP_PING` echoes payload+1; 8-ping stress test green
- [x] **Fd table** — 128 slots, first-fit alloc, states UNBOUND/LISTEN/CONNECTED
- [x] **Stub socket ops** — bind/listen/connect/accept/send/recv non-blocking; two-opener echo test green
- [ ] **Real rump-backed dispatch** — replace stub in `src/phase2_engine/fdtable.c` with calls into rump once rump_init works
- [ ] **`rump_init()` bring-up test** — no attempt yet; blocked on shrinking unresolved-symbol list

### Phase 3 — bsdsocket.library shim
- [x] **`libnetstack_client.a`** — `netstack_init/socket/close/bind/listen/connect/accept/send/recv/ping` wrappers
- [x] **Per-task engine port cache + reply port table** — same pattern as `osal_timer` per-task IORequests
- [x] **`.library` shell — single-interface mode** — resident tag, `MakeLibrary`, `__library` manager works; `OpenLibrary` returns valid base
- [~] **`.library` shell — two-interface mode (`main`)** — enabling the user-facing interface trips a MakeInterface bug that hangs the guest at boot; deferred, callers link `libnetstack_client.a` directly
- [ ] **Full BSD socket API surface** — `WaitSelect`, `getsockopt/setsockopt`, `gethostbyname/inet_addr`/etc.
- [ ] **Per-task errno** — `SocketBaseTags(SBTC_SOCKETSIGNAL)` and `__error()` ptr
- [ ] **Real socket-signal integration** — `WaitSelect` woken on socket-readable

### Phase 4 — NetDev (SANA-III) driver interface
- [x] **Interface headers** — `netdev.h` defines `NetDevCapabilities`, `NetDevBuf`, `NetDevRing`, method table
- [x] **Registry + ring skeleton** — `netdev_registry.c`, `netdev_ring.c` compile
- [x] **Reference driver stub** — `reference_driver.c` (e1000e-shaped vector table, all methods return -1)
- [ ] **Real reference driver** — port `virte1000` to NetDev shape
- [ ] **Zero-copy verification** — mbuf-wrapped NetDevBuf round-trip

### Phase 5 — SANA-II bridge + benchmarks
- [x] **Bridge stub** — `sana2_bridge.c` skeleton
- [ ] **Real bridge** — wrap existing `.device` drivers (`virtnet`, `virte1000`, `loopback`) as NetDev instances
- [ ] **iperf3 client/server on-guest**
- [ ] **Comparative benchmarks vs Roadshow** — same hardware, same workload

### Rump kernel import + compile
- [x] **Import script** — `scripts/import-rump.sh` git-sparse-checkout of `netbsd-10` branch
- [x] **Subtrees imported** — `sys/{rump,net,netinet,netinet6,kern,sys,uvm,dev,secmodel,crypto/*,altq,ufs}`, `sys/arch/powerpc/include`, `sys/lib/libkern`, `common/{include,lib/libc/*}`, `include`; 5990 files, 115 MB
- [x] **Include-path arch symlinks** — `sys/machine` and `sys/powerpc` → `arch/powerpc/include` (rump build makes both; script does it too)
- [x] **`RUMP_CFLAGS` recipe** — `-imacros opt_rumpkernel.h` is the key find; the rest is `-I` plumbing
- [x] **`librump.a` builds** — `scripts/rump-compile-all.sh` compiles 102/114 sources clean → ~1 MB archive
- [x] **Zero unresolved `atomic_*`, `rumpuser_*`, `rump_*`, `VOP_*`, `radix_*` after link** — those categories fully bridged
- [x] **Curated skip-list** — files that fail for architectural reasons (locks_up.c, sleepq.c, vm.c, autogen stubs, disklabel-dependent, ntp compat) explicitly excluded with in-code comments; no more "9 compile failures"
- [x] **opt_*.h stub headers** — 14 empty stubs in `include/rump_opt_stubs/` so files that `#include <opt_*.h>` don't need NetBSD's config generator
- [~] **4 SRCS not found in tree** — `kern_select_50`, `kern_time_50`, `param`, `rndpseudo_50` — compat-50 shims that live under different names in `sys/compat/common`
- [~] **144 remaining unresolved symbols** — down from 188 at session start; breakdown in `docs/rump_compile_report.md`
    - 25 `uvm_*` — needs proper VM plan (arch-specific `VM_{MIN,MAX}_KERNEL_ADDRESS`)
    - 10 `prop_*` — property library source not present in tree
    - 7 `sleepq_*` — provided by the sleepq.c we excluded (needs modern rewrite)
    - long tail (cpu_*, entpool_*, uvmspace_*, pmap_*, ...)
- [ ] **Link `librump.a` + `libnetstack_osal.a` into a test executable** — 153 unresolved must reach 0 first
- [ ] **`rump_init()` at runtime** — must return 0 on the OS4 guest
- [ ] **Compile `sys/net/*.c` and `sys/netinet/*.c`** — Phase 2 network subsystem
- [ ] **Wire `NETSTACK_OP_SOCKET` etc. to `rump_pub_sys_*`** — connect Phase 2 engine to rump for real

### On-guest tests
| Test                            | Status | Verifies                                                   |
|---------------------------------|--------|------------------------------------------------------------|
| `tests/phase1/test_threads`     | Green  | 8 workers create/run/join under mutex; counter matches      |
| `tests/phase1/test_sleep`       | Green  | `osal_sleep_ns` via timer.device; clock proportional        |
| `tests/phase1/test_cv`          | Green  | 3 producers/2 consumers, cv handoff, sums match             |
| `tests/phase2/test_engine_ping` | Green  | Engine spawns, named port, 8 pings round-trip, clean stop   |
| `tests/phase3/test_client_rpc`  | Green  | Client wrappers + per-task reply-port cache; ENOSYS routes  |
| `tests/phase3/test_echo`        | Green  | Full stub-socket bind/listen/connect/accept/send/recv       |
| `tests/phase3/test_bsdlib`      | Partial| OpenLibrary works single-interface; two-interface hangs     |

## Repo layout

```
docs/
  OVERVIEW.md              architecture diagram, component map
  roadmap.md               realistic phase timing + risks
  phase1..5_*.md           per-phase design
  rump_build_probe.md      how the rump compile recipe was derived
  rump_compile_report.md   which imported source files compile today

include/
  netstack/                public headers (osal, mbuf, netdev,
                           netstack, netstack_client, netstack_ipc)
  rumpuser/                NetBSD rump hypercall API declarations

src/
  phase1_osal/             OSAL primitives; rump globals; PPC atomics
  phase2_engine/           netstack.process engine + fd table + IPC dispatch
  phase3_bsdsocket/        libnetstack_client.a + .library shell
  phase4_netdev/           NetDev interface, registry, ring, reference driver
  phase5_testing/          SANA-II bridge stub, bench stub

tests/
  phase1/{test_threads,test_sleep,test_cv}
  phase2/{test_engine_ping}
  phase3/{test_client_rpc,test_echo,test_bsdlib}

scripts/
  build.sh                 docker cross-compile wrapper
  import-rump.sh           fetches NetBSD-10 subtrees under vendor/
  rump-compile-all.sh      compiles rump-kern SRCS, reports pass/fail

vendor/
  netbsd-rump/             NetBSD source subtree (gitignored;
                           regenerated by import-rump.sh)
    IMPORT_MANIFEST.txt    committed — pins commit SHA + counts
```

## Build

```sh
docker pull walkero/amigagccondocker:os4-gcc11-arm64      # once

./scripts/import-rump.sh              # once, fetches ~115 MB source
./scripts/build.sh                    # phases 1-5 objects + libs + tests
./scripts/rump-compile-all.sh         # rump-kern → librump.a
```

Artifacts of interest in `build/`:
- `libnetstack_osal.a` — Phase 1 primitives + PPC atomics + rump globals
- `libnetstack_client.a` — Phase 3 client-side RPC wrappers
- `librump.a` — NetBSD rump kernel subset, 983 KB, 100 objects
- `netstack.library` — Phase 3 `.library` shell (single-interface config)
- `tests/test_*` — on-guest test binaries

## Related projects

- [loopback-amigaos4](https://github.com/geekychris/loopback-amigaos4) —
  pure-software SANA-II loopback. Useful as the RX/TX endpoint for
  Phase 5 stack benchmarks that shouldn't be bottlenecked by hardware.
- [virtnet-amigaos4](https://github.com/geekychris/virtnet-amigaos4) —
  virtio-net SANA-II driver. Reference for a SANA-II-shape driver on
  the OS4 stack.
- [virte1000-amigaos4](https://github.com/geekychris/virte1000-amigaos4) —
  e1000 SANA-II driver.

## License

TBD — likely follow NetBSD's 2-clause BSD for imported code, and
the same for original code, unless there's a strong reason otherwise.
