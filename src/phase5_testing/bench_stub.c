/*
 * phase5_testing/bench_stub.c — placeholder for the benchmark suite.
 *
 * The real benchmarks live under tests/phase5/ (once written) and
 * follow the pattern established by the loopback-amigaos4 project's
 * tests/testperf.c:
 *
 *   - Open a NetDev interface (or via bsdsocket).
 *   - Tight loop of send/recv.
 *   - Time with PPC time-base register (mftb) at 100 MHz.
 *   - Report pkts/sec, MB/sec, µs per packet.
 *
 * This file exists so the phase5 target has something to compile.
 */

int
netstack_bench_placeholder(void)
{
    return 0;
}
