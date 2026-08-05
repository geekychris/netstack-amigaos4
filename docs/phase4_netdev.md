# Phase 4 — NetDev / SANA-III driver framework

## Goal

Define a modern, zero-copy, capability-negotiated driver interface
that replaces (and augments) SANA-II for network drivers on
AmigaOS 4. Ship one reference driver against the spec.

## Non-goals

- Not a wholesale deprecation of SANA-II. Phase 5's SANA-II bridge
  keeps existing drivers working.
- Not a WiFi framework (802.11-family). Ethernet-only for now.
- Not an offload framework (crypto, RDMA, etc.). Room in the
  capabilities struct to add later.

## Primary interfaces

### The interface (`NetDevIF`)

An OS4 `struct Interface` with a vector table of direct function
pointers — no message ports, no `DoIO()` on the hot path.

```c
struct NetDevIF {
    struct InterfaceData Data;

    /* Lifecycle */
    LONG    (*Open)(struct NetDevIF *, ULONG unit, ULONG flags);
    void    (*Close)(struct NetDevIF *);

    /* Capability query — safe to call before Open */
    void    (*GetCapabilities)(struct NetDevIF *, struct NetDevCapabilities *out);

    /* Ring setup — caller allocates the rings and hands them over */
    LONG    (*AttachRings)(struct NetDevIF *, struct NetDevRings *);
    void    (*DetachRings)(struct NetDevIF *);

    /* Hot path */
    LONG    (*Kick)(struct NetDevIF *, ULONG queue);          /* nudge TX */
    ULONG   (*Poll)(struct NetDevIF *, ULONG queue,           /* pull RX */
                    struct NetDevBuf **out, ULONG max);

    /* Control */
    LONG    (*SetMac)(struct NetDevIF *, const UBYTE *mac);
    LONG    (*SetLinkState)(struct NetDevIF *, BOOL up);
    LONG    (*SetMulticast)(struct NetDevIF *, const UBYTE *mac, BOOL add);
    LONG    (*SetPromisc)(struct NetDevIF *, BOOL on);

    /* Statistics */
    void    (*GetStats)(struct NetDevIF *, struct NetDevStats *);
};
```

See [`include/netstack/netdev.h`](../include/netstack/netdev.h) for
the full declarations.

### Capabilities

```c
struct NetDevCapabilities {
    uint32 flags;              /* NETDEV_CAP_* bitmask */
    uint32 rx_queues;
    uint32 tx_queues;
    uint32 max_frame_size;     /* MTU + L2 header, including any offload trailers */
    uint64 link_speed_bps;
    UBYTE  factory_mac[6];
    UBYTE  reserved[2];
};

/* Capability flag bits */
#define NETDEV_CAP_CSUM_IPV4   (1u << 0)
#define NETDEV_CAP_CSUM_TCP    (1u << 1)
#define NETDEV_CAP_CSUM_UDP    (1u << 2)
#define NETDEV_CAP_TSO4        (1u << 3)
#define NETDEV_CAP_TSO6        (1u << 4)
#define NETDEV_CAP_LRO         (1u << 5)
#define NETDEV_CAP_VLAN_TAG    (1u << 6)
#define NETDEV_CAP_SCATTER     (1u << 7)   /* scatter/gather TX */
#define NETDEV_CAP_RSS         (1u << 8)   /* receive-side scaling */
#define NETDEV_CAP_MSIX        (1u << 9)
```

### Ring buffers

- Descriptor rings are shared memory between the driver and the
  stack. Producer writes with a memory barrier; consumer polls with
  a load-acquire.
- Descriptors carry a pointer to a `NetDevBuf` (driver-allocated,
  DMA-safe, refcounted). BSD mbufs wrap the same memory via
  external cluster mbufs — no copy on the TX or RX path.
- Ring sizes negotiated at `AttachRings`. Driver publishes maxima
  in `NetDevCapabilities`; stack picks actual sizes.

### The buffer (`NetDevBuf`)

```c
struct NetDevBuf {
    APTR    data;               /* virtual address; ring holds this */
    APTR    phys;               /* DMA-usable physical address */
    ULONG   len;                /* used bytes */
    ULONG   size;               /* allocated bytes */
    ULONG   flags;              /* NETDEV_BUF_* */
    APTR    driver_priv;        /* opaque to stack */
    LONG    refcount;
    /* pool it came from, so free_buf knows how to return it */
    void  (*free)(struct NetDevBuf *);
};
```

## Reference driver

`src/phase4_netdev/reference_e1000e/` — a NetDev implementation
of the Intel e1000e chipset. Chosen because we already have a
working SANA-II e1000 driver (virte1000) to cross-reference against.

Milestones:
1. Reset + init on X5000 or QEMU sam460ex.
2. RX ring setup, single-packet receive using zero-copy.
3. TX ring setup, single-packet send.
4. Interrupt handling (MSI-X preferred if the board supports it).
5. Full capability advertisement + offload plumbing.

## Testing strategy

- `tests/phase4/test_capabilities.c`: open the reference driver,
  print capabilities. Sanity.
- `tests/phase4/test_ring_setup.c`: attach rings, verify producer
  and consumer indexes both start at 0.
- `tests/phase4/test_loopback_wire.c`: reference driver in
  internal-loopback mode (e1000e supports it via a self-loop bit)
  — TX a frame, expect it on RX ring. No wire needed.

## Known-hard bits

- **DMA physical addresses.** On sam460ex without an IOMMU, CPU
  virt ≠ PCI phys. `IExec->StartDMA` / `GetDMAList` are the
  OS-blessed path. Getting this consistent for all pool allocs is
  make-or-break.
- **Cache coherency.** PPC 460 has separate I- and D-caches. NIC
  writes to RAM (RX) must be `dcbi`/`sync`'d before CPU reads;
  CPU writes to TX descriptors must be `dcbf`/`sync`'d before
  the NIC reads. Our virtnet driver already learned all these
  lessons — steal the primitives.
- **Interrupt priority.** OS4 IRQs run at whatever priority the
  handler is registered at. NIC IRQ must be higher than
  netstack.process but lower than any hard-realtime kernel work.
- **Multi-queue.** RSS and multiple RX rings are Ethernet-standard
  today but not something SANA-II ever addressed. NetDev must
  support this from day one or we'll bolt-on later and regret it.

## Current status

**Header spec + stubs.** [`include/netstack/netdev.h`](../include/netstack/netdev.h)
compiles clean. `src/phase4_netdev/*.c` has function stubs that
return `-1`. No reference driver code yet.
