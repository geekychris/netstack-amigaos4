# netstack-amigaos4 — modern BSD network stack for AmigaOS 4

**Status: SKELETON.** Directory layout, headers, stubs, and per-phase
docs are in place. **No working code yet.** This repo is the
scaffold that Phase 1 begins to fill in.

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

## What this is NOT (yet)

- Not compiled BSD code — `vendor/netbsd-rump/` is empty; import is
  scripted but not run.
- Not a shippable driver — Phase 4 is a header spec and stubs.
- Not benchmarked — Phase 5 is scaffolding only.

See [`docs/OVERVIEW.md`](docs/OVERVIEW.md) for the architecture
picture and [`docs/roadmap.md`](docs/roadmap.md) for realistic time
estimates (spoiler: multiple quarters of full-time work).

## Reality check

Porting a modern BSD network stack is a multi-quarter effort even
with NetBSD rump kernels doing most of the isolation work. This
repo is here so that when the effort starts, the scaffolding
already exists and each phase has clear boundaries. Do not confuse
"skeleton complete" with "stack working" — those are separated by
somewhere between 6 and 18 months of steady kernel work.

## Choice of upstream

**NetBSD rump kernel**, not FreeBSD extraction. NetBSD's rump
framework is purpose-built for hosting kernel subsystems outside
their native kernel: `sys/net`, `sys/netinet`, `sys/netinet6` are
already isolated behind a ~50-function `rumpuser_*` hypercall
API. The OSAL surface area is a fraction of what a raw FreeBSD
extraction would require. See
[`docs/phase1_osal.md`](docs/phase1_osal.md) for the specifics.

Trade-off accepted: NetBSD's TCP is fine but doesn't have BBR/RACK
today. If those become dealbreakers for a specific use case they
can be back-ported later — porting the RACK code is far less work
than porting the whole FreeBSD stack.

## Repo layout

```
docs/
  OVERVIEW.md           — architecture diagram, component map
  roadmap.md            — realistic phase timing + risks
  phase1_osal.md        — OS Adaptation Layer
  phase2_engine.md      — netstack.process
  phase3_bsdsocket.md   — bsdsocket.library shim
  phase4_netdev.md      — NetDev / SANA-III driver spec
  phase5_testing.md     — SANA-II bridge, iperf3, tuning
include/
  netstack/             — public headers for consumers of this repo
  rumpuser/             — hypercall API we implement for the rump kernel
src/
  phase1_osal/          — malloc, mutex, thread, timer, mbuf → ExecSG
  phase2_engine/        — netstack.process main loop
  phase3_bsdsocket/     — bsdsocket.library Interface vector table
  phase4_netdev/        — NetDev interface + reference driver
  phase5_testing/       — SANA-II bridge, benchmarks
tests/                  — per-phase test harnesses
scripts/
  build.sh              — docker cross-compile wrapper
  import-rump.sh        — placeholder for NetBSD rump source import
vendor/
  netbsd-rump/          — NetBSD source subtree (import script writes here)
```

## Build

```sh
docker pull walkero/amigagccondocker:os4-gcc11-arm64   # once
./scripts/build.sh                                     # builds what compiles today
```

Right now `scripts/build.sh` builds Phase 1 stubs and Phase 4
headers, and reports the rest as `not-yet`. Each phase adds itself
to the top-level Makefile as it starts producing output.

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
