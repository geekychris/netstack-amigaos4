/*
 * phase1_osal/rump_atomics.c — PPC implementations of NetBSD's
 * atomic_*() family.
 *
 * NetBSD's kernel expects atomic ops on 32-bit ints, uints,
 * longs, and pointers. On PPC we implement each via the classic
 * lwarx / stwcx. reservation loop. For a single-CPU rump kernel
 * this could be simpler (Forbid()/Permit() around a normal RMW),
 * but the lwarx/stwcx pattern is correct on any # of CPUs and
 * doesn't require rump-context awareness.
 *
 * Only the 32-bit ops that the compiled librump.a actually
 * references are implemented here. The full NetBSD atomic API is
 * huge; expand on demand.
 */

#include <stdint.h>
#include <stddef.h>

/* ---- Compare-and-swap ---------------------------------------- */

uint32_t
atomic_cas_32(volatile uint32_t *ptr, uint32_t old, uint32_t new)
{
    uint32_t prev;
    __asm__ __volatile__ (
        "1: lwarx   %0, 0, %1\n"
        "   cmpw    %0, %2\n"
        "   bne     2f\n"
        "   stwcx.  %3, 0, %1\n"
        "   bne-    1b\n"
        "2:\n"
        : "=&r"(prev)
        : "r"(ptr), "r"(old), "r"(new)
        : "cc", "memory");
    return prev;
}

unsigned int
atomic_cas_uint(volatile unsigned int *ptr, unsigned int old, unsigned int new)
{
    return (unsigned int)atomic_cas_32((volatile uint32_t *)ptr,
                                       (uint32_t)old, (uint32_t)new);
}

void *
atomic_cas_ptr(volatile void *ptr, void *old, void *new)
{
    return (void *)(uintptr_t)atomic_cas_32(
        (volatile uint32_t *)ptr, (uint32_t)(uintptr_t)old,
        (uint32_t)(uintptr_t)new);
}

/* ---- Fetch-and-op ------------------------------------------- */

static inline uint32_t
atomic_fetchop_32(volatile uint32_t *ptr, uint32_t operand, int op)
{
    uint32_t prev, next;
    __asm__ __volatile__ (
        "1: lwarx   %0, 0, %2\n"
        : "=&r"(prev) : "r"(prev), "r"(ptr));
    for (;;) {
        switch (op) {
            case 0: next = prev + operand; break;   /* add */
            case 1: next = prev | operand; break;   /* or */
            case 2: next = prev & operand; break;   /* and */
            default: next = prev; break;
        }
        uint32_t got;
        __asm__ __volatile__ (
            "   stwcx.  %2, 0, %3\n"
            "   bne-    1f\n"
            "   mr      %0, %4\n"
            "   b       2f\n"
            "1: lwarx   %0, 0, %3\n"
            "2:\n"
            : "=&r"(got)
            : "0"(0), "r"(next), "r"(ptr), "r"(next)
            : "cc", "memory");
        if (got == next) return prev;
        prev = got;
    }
}

/* ---- Add ---------------------------------------------------- */

void
atomic_add_int(volatile unsigned int *ptr, int val)
{
    uint32_t old, new;
    do {
        old = *ptr;
        new = old + (uint32_t)val;
    } while (atomic_cas_32((volatile uint32_t *)ptr, old, new) != old);
}

void
atomic_add_long(volatile unsigned long *ptr, long val)
{
    /* On PPC32, long == int == 32 bits. */
    atomic_add_int((volatile unsigned int *)ptr, (int)val);
}

unsigned long
atomic_add_long_nv(volatile unsigned long *ptr, long val)
{
    uint32_t old, new;
    do {
        old = *(volatile uint32_t *)ptr;
        new = old + (uint32_t)val;
    } while (atomic_cas_32((volatile uint32_t *)ptr, old, new) != old);
    return new;
}

/* ---- Inc / Dec ---------------------------------------------- */

void
atomic_inc_uint(volatile unsigned int *ptr)
{
    atomic_add_int(ptr, 1);
}

unsigned int
atomic_inc_uint_nv(volatile unsigned int *ptr)
{
    uint32_t old, new;
    do {
        old = *(volatile uint32_t *)ptr;
        new = old + 1;
    } while (atomic_cas_32((volatile uint32_t *)ptr, old, new) != old);
    return new;
}

void
atomic_dec_uint(volatile unsigned int *ptr)
{
    atomic_add_int(ptr, -1);
}

unsigned int
atomic_dec_uint_nv(volatile unsigned int *ptr)
{
    uint32_t old, new;
    do {
        old = *(volatile uint32_t *)ptr;
        new = old - 1;
    } while (atomic_cas_32((volatile uint32_t *)ptr, old, new) != old);
    return new;
}

/* ---- Bitwise -------------------------------------------------- */

void
atomic_or_32(volatile uint32_t *ptr, uint32_t val)
{
    uint32_t old, new;
    do {
        old = *ptr;
        new = old | val;
    } while (atomic_cas_32(ptr, old, new) != old);
}

void
atomic_or_uint(volatile unsigned int *ptr, unsigned int val)
{
    atomic_or_32((volatile uint32_t *)ptr, (uint32_t)val);
}

void
atomic_and_32(volatile uint32_t *ptr, uint32_t val)
{
    uint32_t old, new;
    do {
        old = *ptr;
        new = old & val;
    } while (atomic_cas_32(ptr, old, new) != old);
}

void
atomic_and_uint(volatile unsigned int *ptr, unsigned int val)
{
    atomic_and_32((volatile uint32_t *)ptr, (uint32_t)val);
}

/* ---- Swap ---------------------------------------------------- */

unsigned int
atomic_swap_uint(volatile unsigned int *ptr, unsigned int val)
{
    uint32_t old;
    do {
        old = *(volatile uint32_t *)ptr;
    } while (atomic_cas_32((volatile uint32_t *)ptr, old, val) != old);
    return old;
}

void *
atomic_swap_ptr(volatile void *ptr, void *val)
{
    return (void *)(uintptr_t)atomic_swap_uint(
        (volatile unsigned int *)ptr, (unsigned int)(uintptr_t)val);
}

/* ---- Byte-swap ------------------------------------------------ */

uint32_t
bswap32(uint32_t v)
{
    return ((v & 0xFF000000u) >> 24) | ((v & 0x00FF0000u) >>  8) |
           ((v & 0x0000FF00u) <<  8) | ((v & 0x000000FFu) << 24);
}

uint64_t
bswap64(uint64_t v)
{
    return ((uint64_t)bswap32((uint32_t)(v & 0xFFFFFFFFu)) << 32) |
           (uint64_t)bswap32((uint32_t)(v >> 32));
}
