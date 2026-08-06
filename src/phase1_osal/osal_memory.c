/*
 * phase1_osal/osal_memory.c — memory hypercalls.
 *
 * STATUS: functional stubs — actually allocate/free but with no
 * DMA-alignment awareness and no accounting. Enough to satisfy
 * rump_init()'s early bring-up calls in Phase 2 smoke tests.
 */

#include "netstack/osal.h"
#include "rumpuser/rumpuser.h"

#include <proto/exec.h>
#include <exec/exectags.h>
#include <string.h>

void *
osal_malloc(size_t size, size_t align)
{
    if (align == 0) align = 16;
    extern void osal_trace(const char *fmt, ...);
    void *p = IExec->AllocVecTags(size,
        AVT_Type,           MEMF_SHARED,
        AVT_Alignment,      align,
        AVT_ClearWithValue, 0,
        TAG_END);
    osal_trace("[malloc] sz=%lu align=%lu -> %p\n",
               (unsigned long)size, (unsigned long)align, p);
    return p;
}

void
osal_free(void *ptr)
{
    if (ptr) IExec->FreeVec(ptr);
}

/*
 * DMA-safe allocation. Phase 4 will replace this with a
 * physically-contiguous pool + StartDMA/GetDMAList integration.
 * Today: identical to osal_malloc(). Fine for loopback tests;
 * broken for actual NIC DMA. TODO(phase4).
 */
void *
osal_dma_malloc(size_t size, size_t align)
{
    return osal_malloc(size, align);
}

void
osal_dma_free(void *ptr)
{
    osal_free(ptr);
}

/* -------- rump hypercall shims (map onto osal_*) ---------------- */

int
rumpuser_malloc(size_t howmuch, int alignment, void **memp)
{
    void *p = osal_malloc(howmuch, alignment > 0 ? (size_t)alignment : 16);
    if (!p) return 12 /* ENOMEM */;
    *memp = p;
    return 0;
}

void
rumpuser_free(void *ptr, size_t size)
{
    (void)size;
    osal_free(ptr);
}

int
rumpuser_anonmmap(void *prefaddr, size_t size, int alignbit,
                  int exec, void **memp)
{
    (void)prefaddr; (void)exec;
    size_t align = alignbit > 0 ? ((size_t)1 << alignbit) : 4096;
    void *p = osal_malloc(size, align);
    if (!p) return 12;
    *memp = p;
    return 0;
}

void
rumpuser_unmap(void *addr, size_t size)
{
    (void)size;
    osal_free(addr);
}
