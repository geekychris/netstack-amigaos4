# Phase 5 — SANA-II bridge, testing, tuning

## Goal

Two things:

1. **SANA-II legacy bridge** so existing `.device` drivers (virtnet,
   virte1000, rtl8139, etc.) work with the new stack unchanged.
2. **Benchmarks + tuning** — measure throughput vs Roadshow / AmiTCP
   on the same hardware, close the gap where possible.

## Non-goals

- Not a rewrite of any existing SANA-II driver.
- Not a general-purpose performance profiler; just enough measurement
  to know if we're winning or losing vs the incumbent.

## SANA-II bridge

`src/phase5_testing/sana2_bridge.c` implements a `NetDevIF` (Phase 4
interface) that internally speaks SANA-II (Phase 4-and-older
interface) to a chosen `.device`:

- On `Kick(TX)`: dequeue frames from the ring, allocate an IOSana2Req,
  fill it, `DoIO()` or `SendIO()` via the wrapped device.
- On device `CMD_READ` completion: enqueue frame onto the RX ring.
- Capabilities advertised: whatever the SANA-II device supports
  (typically ethernet, no checksum offload, single queue).

Users configure a bridge instance via `NetstackConfig`:

```c
struct NetstackBridgeConfig {
    STRPTR   sana2_device;   /* e.g. "virtnet.device" */
    ULONG    unit;
    STRPTR   bridge_name;    /* NetDev name presented to the stack */
};
```

This is a stopgap. Frames pay one memcpy each way (SANA-II copy
hooks require a callback-driven copy). Real zero-copy requires
Phase 4 native drivers.

## Benchmark suite

`src/phase5_testing/bench/` — a collection of small utilities
run on the OS4 guest that report reproducible numbers:

- `bench_udp_throughput` — flood UDP to a specified peer, measure
  send-side throughput.
- `bench_tcp_iperf3_client` — thin wrapper around the standard
  iperf3 protocol, run against a real iperf3 server on a Linux/BSD
  host over the same LAN.
- `bench_cpu_overhead` — measure CPU usage during a fixed-rate
  transfer; compare against Roadshow doing the same.
- `bench_latency` — round-trip ping-style measurement to a peer,
  distribution histogram.

Numbers reported: throughput (Mbit/s), CPU % during transfer,
round-trip latency (µs) mean/p50/p99, packet loss %, retransmit %.

Baselines to compare against:

| Baseline           | NIC              | Setup                              |
|--------------------|------------------|------------------------------------|
| Roadshow + virtnet | QEMU virtio-net  | Guest → host iperf3 server         |
| Roadshow + e1000   | QEMU e1000e      | Guest → host iperf3 server         |
| netstack + bridge  | Same NICs        | Should match Roadshow within noise |
| netstack + NetDev  | Same NICs        | Should beat Roadshow by 30%+       |
| loopback.device    | none             | Ceiling of SANA-II copy-hook loop  |

## Tuning knobs

Documented in `docs/tuning.md` (not yet written). Highlights:

- `sysctl net.inet.tcp.sendspace` / `recvspace` — TCP buffer sizes.
- `sysctl net.inet.tcp.mss_ifmtu` — force MSS from interface MTU.
- rump mbuf pool sizing — `RUMP_NCPU`-related.
- netstack.process priority — trade responsiveness for throughput.
- IRQ coalescing settings on NetDev drivers.

## Testing strategy

- Every commit that touches Phase 4/5 code re-runs the four
  benchmarks against a fixed golden baseline; regressions fail CI.
- Human-in-the-loop: a `perf-report` script that generates a
  markdown table from a fresh benchmark run for including in PRs.

## Current status

**Stub.** SANA-II bridge is header + skeleton only. Benchmarks
directory exists with README pointers to the driver-level
`testperf` from
[loopback-amigaos4](https://github.com/geekychris/loopback-amigaos4)
as a proof-of-concept template.
