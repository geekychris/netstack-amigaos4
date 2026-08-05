/*
 * phase1_osal/mbuf_pool.c — mbuf cluster pool.
 *
 * STATUS: stub. Real impl:
 *   - Fixed-size cluster pool (2048B typical), lockless SLAB.
 *   - Backing memory from osal_dma_malloc so clusters can be
 *     handed to NetDev drivers zero-copy.
 *   - Growth-on-demand up to a configured cap.
 *   - Refcount + free-callback on external-cluster mbufs so a
 *     driver can free the underlying buffer when its refcount hits 0.
 */

#include "netstack/mbuf.h"
#include "netstack/osal.h"

int
netstack_mbuf_pool_init(size_t cluster_size, size_t initial_clusters,
                        size_t max_clusters, size_t dma_alignment)
{
    (void)cluster_size; (void)initial_clusters;
    (void)max_clusters; (void)dma_alignment;
    /* TODO: allocate initial_clusters via osal_dma_malloc,
     * chain into free-list. */
    return 0;
}

void
netstack_mbuf_pool_shutdown(void)
{
    /* TODO: walk free-list, osal_dma_free each. */
}

struct mbuf *
netstack_mbuf_from_netdevbuf(struct NetDevBuf *b)
{
    (void)b;
    /* TODO: allocate an mbuf head, point m_ext at b->data,
     * install refcount hook. */
    return 0;
}

struct NetDevBuf *
netstack_mbuf_to_netdevbuf(struct mbuf *m)
{
    (void)m;
    return 0;
}
