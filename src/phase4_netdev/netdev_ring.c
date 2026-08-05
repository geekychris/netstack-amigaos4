/*
 * phase4_netdev/netdev_ring.c — SPSC lockless descriptor ring.
 *
 * STATUS: skeleton with correct sequence-number arithmetic but
 * MISSING the memory barriers (eieio/sync) required for PPC
 * weak-ordering safety. Comments mark the barrier sites.
 */

#include "netstack/netdev.h"
#include "netstack/osal.h"

/* -------- PPC memory barrier stubs ---------------------------- */

/* TODO: inline asm eieio + sync. For now these are compiler-only. */
#define barrier_acquire()  __asm__ __volatile__ ("" ::: "memory")
#define barrier_release()  __asm__ __volatile__ ("" ::: "memory")

/* -------- Producer side (driver on RX, stack on TX) ----------- */

int
netdev_ring_publish(struct NetDevRing *r, struct NetDevBuf *b)
{
    uint64_t head = r->head;
    uint64_t tail = r->tail;
    if (head - tail >= r->ring_size) return -1;   /* full */

    r->slots[head % r->ring_size] = b;
    barrier_release();
    r->head = head + 1;
    return 0;
}

/* -------- Consumer side (stack on RX, driver on TX-complete) --- */

int
netdev_ring_consume(struct NetDevRing *r, struct NetDevBuf **out)
{
    uint64_t tail = r->tail;
    uint64_t head = r->head;
    if (tail == head) return -1;   /* empty */

    barrier_acquire();
    *out = r->slots[tail % r->ring_size];
    r->tail = tail + 1;
    return 0;
}

/* -------- Init ------------------------------------------------ */

int
netdev_ring_init(struct NetDevRing *r, uint32_t ring_size)
{
    if (ring_size == 0 || (ring_size & (ring_size - 1)) != 0)
        return -1;   /* must be power of 2 */
    r->slots = osal_malloc(sizeof(struct NetDevBuf *) * ring_size, 64);
    if (!r->slots) return -1;
    r->ring_size = ring_size;
    r->head = r->tail = 0;
    return 0;
}

void
netdev_ring_destroy(struct NetDevRing *r)
{
    if (r->slots) osal_free(r->slots);
    r->slots = 0;
}
