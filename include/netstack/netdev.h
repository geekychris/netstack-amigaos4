/*
 * netstack/netdev.h — modern zero-copy driver interface (SANA-III).
 *
 * The Phase 4 replacement for SANA-II. Drivers implement a
 * struct Interface (OS4-native) with function pointers that the
 * netstack calls directly — no message ports, no DoIO() on the
 * hot path. Descriptor rings are shared memory. Frame buffers
 * are driver-allocated and wrapped as external-cluster mbufs
 * with zero copy on the fast path.
 *
 * STATUS: header only. src/phase4_netdev/ has skeleton stubs
 * for the interface constructor and a placeholder reference
 * driver.
 */

#ifndef NETSTACK_NETDEV_H
#define NETSTACK_NETDEV_H

#include <exec/types.h>
#include <exec/interfaces.h>   /* struct InterfaceData */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward decls — full definitions below. */
struct NetDevIF;
struct NetDevBuf;
struct NetDevRings;
struct NetDevCapabilities;
struct NetDevStats;

/* -------- Capability advertisement ------------------------------ */

/* Flag bits in NetDevCapabilities.flags. */
#define NETDEV_CAP_CSUM_IPV4   (1u << 0)  /* HW computes IPv4 header cksum */
#define NETDEV_CAP_CSUM_TCP    (1u << 1)  /* HW computes TCP cksum */
#define NETDEV_CAP_CSUM_UDP    (1u << 2)  /* HW computes UDP cksum */
#define NETDEV_CAP_TSO4        (1u << 3)  /* HW TCP segmentation offload v4 */
#define NETDEV_CAP_TSO6        (1u << 4)  /* HW TCP segmentation offload v6 */
#define NETDEV_CAP_LRO         (1u << 5)  /* HW large-receive offload */
#define NETDEV_CAP_VLAN_TAG    (1u << 6)  /* HW 802.1Q insertion/strip */
#define NETDEV_CAP_SCATTER     (1u << 7)  /* Scatter/gather TX (multi-desc frames) */
#define NETDEV_CAP_RSS         (1u << 8)  /* Receive-side scaling (multi RX queue) */
#define NETDEV_CAP_MSIX        (1u << 9)  /* MSI-X supported */

struct NetDevCapabilities {
    uint32 flags;
    uint32 rx_queues;         /* max concurrent RX rings */
    uint32 tx_queues;         /* max concurrent TX rings */
    uint32 max_frame_size;    /* incl. L2 header, tags, checksums */
    uint64 link_speed_bps;    /* nominal, e.g. 1000000000 for 1 GbE */
    UBYTE  factory_mac[6];
    UBYTE  reserved[2];
};

/* -------- Statistics -------------------------------------------- */

struct NetDevStats {
    uint64 rx_packets;
    uint64 rx_bytes;
    uint64 rx_errors;
    uint64 rx_dropped;
    uint64 tx_packets;
    uint64 tx_bytes;
    uint64 tx_errors;
    uint64 tx_dropped;
    uint64 rx_multicast;
    uint64 tx_multicast;
    uint64 rx_broadcast;
    uint64 tx_broadcast;
    uint64 collisions;
};

/* -------- Frame buffer ------------------------------------------ */

/* NetDevBuf flags. */
#define NETDEV_BUF_CSUM_OK       (1u << 0)  /* HW verified all L3/L4 checksums */
#define NETDEV_BUF_CSUM_BAD      (1u << 1)  /* HW says checksum bad */
#define NETDEV_BUF_VLAN_STRIPPED (1u << 2)  /* HW stripped a VLAN tag */

struct NetDevBuf {
    APTR    data;             /* CPU-virt start of payload */
    uint64  phys;             /* PCI-DMA address of data */
    uint32  len;              /* used bytes */
    uint32  size;             /* total allocated bytes */
    uint32  flags;            /* NETDEV_BUF_* */
    uint16  vlan_tag;         /* if VLAN_STRIPPED, the stripped tag */
    uint16  queue;            /* which RX queue delivered (RSS) */
    APTR    driver_priv;      /* opaque to the stack */
    LONG    refcount;
    void  (*free)(struct NetDevBuf *);   /* return to driver pool */
};

/* -------- Descriptor rings -------------------------------------- */

/*
 * NetDev ring is a SPSC lockless ring of NetDevBuf pointers.
 * Producer writes with a release barrier; consumer reads with an
 * acquire barrier. head/tail are 64-bit sequence numbers; index
 * into the ring is (seq % ring_size).
 */
struct NetDevRing {
    struct NetDevBuf **slots;  /* ring_size entries */
    uint32            ring_size;
    volatile uint64   head;    /* producer writes */
    volatile uint64   tail;    /* consumer writes */
    uint32            pad[2];
};

struct NetDevRings {
    struct NetDevRing *tx;     /* one per TX queue */
    uint32             n_tx;
    struct NetDevRing *rx;     /* one per RX queue */
    uint32             n_rx;
};

/* -------- The interface ---------------------------------------- */

struct NetDevIF {
    struct InterfaceData Data;

    /* Lifecycle */
    LONG    APICALL (*Open)(struct NetDevIF *, ULONG unit, ULONG flags);
    void    APICALL (*Close)(struct NetDevIF *);

    /* Capability query — safe to call before Open. */
    void    APICALL (*GetCapabilities)(struct NetDevIF *,
                                       struct NetDevCapabilities *out);

    /* Ring attach — caller allocates the rings and hands them over. */
    LONG    APICALL (*AttachRings)(struct NetDevIF *, struct NetDevRings *);
    void    APICALL (*DetachRings)(struct NetDevIF *);

    /* Buffer allocation from the driver's DMA-safe pool. Callers use
     * this on TX (fill the buf, publish to a TX ring, Kick). Drivers
     * fill RX rings from the same pool internally. */
    struct NetDevBuf *APICALL (*AllocBuf)(struct NetDevIF *, ULONG size);
    void    APICALL (*FreeBuf)(struct NetDevIF *, struct NetDevBuf *);

    /* Hot path */
    LONG    APICALL (*Kick)(struct NetDevIF *, ULONG queue);
    ULONG   APICALL (*Poll)(struct NetDevIF *, ULONG queue,
                            struct NetDevBuf **out, ULONG max);

    /* Interrupt integration. Register a callback (from a task
     * context) that fires when the driver has RX data or a TX
     * completion. Callback should just Signal() the stack's
     * netstack.process. */
    LONG    APICALL (*RegisterKick)(struct NetDevIF *,
                                    void (*cb)(APTR ctx), APTR ctx);

    /* Control */
    LONG    APICALL (*SetMac)(struct NetDevIF *, const UBYTE *mac);
    LONG    APICALL (*SetLinkState)(struct NetDevIF *, BOOL up);
    LONG    APICALL (*SetMulticast)(struct NetDevIF *,
                                    const UBYTE *mac, BOOL add);
    LONG    APICALL (*SetPromisc)(struct NetDevIF *, BOOL on);

    /* Statistics — snapshot into caller-provided struct. */
    void    APICALL (*GetStats)(struct NetDevIF *, struct NetDevStats *);
};

/* -------- Registration ----------------------------------------- */

/*
 * Called by drivers at library-init time to advertise a NetDev
 * instance. The stack calls Open() on drivers when the user brings
 * an interface up.
 */
LONG NetDev_Register(const char *name, struct NetDevIF *iface);
LONG NetDev_Unregister(const char *name);

/* Enumerate registered drivers. */
struct NetDevIF *NetDev_Lookup(const char *name);
LONG             NetDev_List(char *out_names[], LONG max_names);

#ifdef __cplusplus
}
#endif

#endif /* NETSTACK_NETDEV_H */
