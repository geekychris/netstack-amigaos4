/*
 * netstack/mbuf.h — mbuf wrapper.
 *
 * We do not redefine struct mbuf; the imported NetBSD rump kernel
 * ships the canonical definition in <sys/mbuf.h>. This header
 * exposes the *pool management* wrapper we own on the AmigaOS side:
 *
 *   - The backing allocator (DMA-safe cluster pool).
 *   - A NetDevBuf ↔ mbuf zip so Phase 4 drivers can wrap driver-
 *     allocated frame buffers as external-cluster mbufs without
 *     any memcpy.
 *
 * STATUS: header + stubs. Real integration happens after the rump
 * kernel is imported and we can #include its <sys/mbuf.h>.
 */

#ifndef NETSTACK_MBUF_H
#define NETSTACK_MBUF_H

#include <exec/types.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward decls — real defs come from the rump kernel's <sys/mbuf.h>. */
struct mbuf;
struct NetDevBuf;

/* -------- Pool ---------------------------------------------------- */

/*
 * Initialize the mbuf cluster pool. Called once from Phase 2 engine
 * startup before rump_init().
 *
 * `cluster_size` typically 2048 (MCLBYTES). `initial_clusters` is
 * a warm-cache hint; the pool grows on demand up to `max_clusters`.
 * `dma_alignment` is the alignment DMA-visible allocations must
 * satisfy (e.g., cache-line size).
 */
int netstack_mbuf_pool_init(size_t cluster_size,
                            size_t initial_clusters,
                            size_t max_clusters,
                            size_t dma_alignment);

void netstack_mbuf_pool_shutdown(void);

/* -------- NetDevBuf zip ----------------------------------------- */

/*
 * Wrap a driver-owned NetDevBuf as an external-cluster mbuf.
 * Increments the NetDevBuf refcount; the mbuf's free callback
 * decrements it and returns the buffer to the driver's pool.
 * Used by NetDev drivers on RX and by the stack on TX to hand
 * memory straight to the driver.
 */
struct mbuf *netstack_mbuf_from_netdevbuf(struct NetDevBuf *);

/*
 * The reverse: peek at the NetDevBuf backing an mbuf (or NULL
 * if the mbuf isn't external-cluster / doesn't belong to us).
 */
struct NetDevBuf *netstack_mbuf_to_netdevbuf(struct mbuf *);

#ifdef __cplusplus
}
#endif

#endif /* NETSTACK_MBUF_H */
