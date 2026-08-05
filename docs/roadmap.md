# Roadmap and realistic time estimates

Estimates are for a single experienced kernel-C developer working
full-time. Multiply by 2-3× for part-time / evenings, and add 20-50%
for unfamiliarity with either BSD internals or AmigaOS 4 internals.

| Phase | Milestone                                       | Full-time estimate |
|-------|-------------------------------------------------|--------------------|
| 1     | OSAL: all ~50 `rumpuser_*` implemented + tested | 4–6 weeks          |
| 2     | Rump kernel boots inside `netstack.process`; internal loopback ping (127.0.0.1 through the stack) passes | 4–6 weeks |
| 3     | `bsdsocket.library` shim: existing app (e.g. `ping`, `wget`) works end-to-end via the new stack | 4–8 weeks |
| 4     | `NetDev` spec finalized, one reference driver (e.g. e1000e) works with zero-copy TX/RX | 6–10 weeks |
| 5     | SANA-II bridge; `iperf3` runs; tune to within N% of Roadshow's throughput on same NIC | 4–6 weeks |
|       | **Total (serial)**                              | **22–36 weeks** (5–8 months full time) |

Non-serial dependencies:
- Phase 4 can start after Phase 1 (doesn't need Phase 2/3 running).
- Phase 5 SANA-II bridge can start immediately (it's a Phase 4
  interface + existing SANA-II protocol).

## Highest-risk items

Ranked by "will this eat months if it goes wrong."

1. **`rumpuser_*` semantics mismatch.** Some hypercalls have subtle
   ordering / re-entrancy contracts (e.g., `rumpuser_thread_join`,
   `rumpuser_cv_wait` interruption) that are easy to get 90% right
   and stall on 10% of the code paths. Rump kernel test suite is
   the safety net.
2. **mbuf allocator + DMA alignment.** Phase 4 zero-copy requires
   mbufs whose external cluster memory is physically-contiguous and
   suitably aligned. On sam460ex with no IOMMU, DMA physical
   addresses need care — CachePreDMA / GetDMAList / StartDMA. Get
   this wrong and packets silently corrupt.
3. **PPC weak-memory ordering vs BSD assumptions.** NetBSD's SMP
   locking assumes x86-strong-order in a few dark corners; PPC's
   weakly-ordered memory needs `eieio` / `sync` barriers in the
   OSAL wrapper. Bugs here are heisenbugs.
4. **`bsdsocket.library` fd namespace.** OS4 apps expect file
   descriptors that survive across `WaitSelect()`, integrate with
   `select()`, and interoperate with DOS file handles in some
   third-party APIs. Getting this "unsurprising" for existing apps
   is not the same as "correct per POSIX."
5. **Toolchain limits.** walkero's gcc 11 + newlib may reject some
   NetBSD headers (GCC-specific pragmas, `__weak_alias`, etc).
   May need a small compat header layer or patched imports.

## Non-goals (deliberately excluded)

- Kernel-mode operation. This runs as a normal OS4 process. No
  Kickstart-time init.
- Backwards compat with AmiTCP-specific APIs beyond bsdsocket.
- IPv6 first-class support in Phase 1-3. Included in the imports
  (NetBSD ships v6 in the same tree) but not tested/tuned until
  Phase 5.
- WiFi / 802.11. Wired Ethernet only. NetBSD's net80211 stack could
  be added later but doubles Phase 4 scope.
- IPsec / crypto offload.

## What "done" for each phase looks like

- **P1 done**: NetBSD rump kernel test suite (`rump_server` +
  `librumpnet_config`) links and runs on OS4; the standard rump
  self-tests pass.
- **P2 done**: `netstack.process` starts, initializes rump, opens
  a `PF_INET` socket internally, sends a UDP packet to a loopback
  address, receives it. Purely internal — no wire, no bsdsocket.
- **P3 done**: `wget http://<some-ip>` from a standard OS4 shell
  works, using our `bsdsocket.library` and our stack. Compare
  headers + body to a Roadshow reference.
- **P4 done**: `iperf3` between an OS4 machine (new stack + new
  driver) and a Linux host over a real NIC hits ≥80% of the same
  NIC's Roadshow number.
- **P5 done**: Any existing SANA-II driver (say, virtnet.device)
  works with the new stack via the SANA-II bridge, without
  modification.

## Kill criteria

Reasons to stop and re-plan rather than push through:

- OSAL Phase 1 exceeds 12 weeks without rump self-tests passing.
  Signal that the hypercall model doesn't fit ExecSG cleanly and a
  different porting strategy (raw FreeBSD? Older AmiTCP fork?)
  should be considered.
- If bsdsocket.library shim in Phase 3 breaks IBrowse / Odyssey in
  ways that require app-level patches, the compat story is broken
  and the value proposition is diminished.
- If Phase 4 zero-copy throughput fails to beat SANA-II bridge
  throughput by ≥2×, the new driver interface isn't worth the
  ecosystem cost of introducing.
