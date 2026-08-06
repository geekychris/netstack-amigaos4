/*
 * phase1_osal/rump_link_stubs.c — trivial stubs for the 108
 * unresolved symbols on the rump_init() link path.
 *
 * Goal: make the LINK succeed. RUNTIME will crash the first
 * time any of these is called for real — that's fine at this
 * stage; we're proving the toolchain + archives are shape-
 * complete, not that the kernel works yet.
 *
 * Each stub either:
 *   - returns 0 (functions returning int)
 *   - returns NULL (functions returning pointer)
 *   - returns void
 *   - is defined as zero-initialized data
 *
 * When the real subsystem is ported (VM, sleepq, property list,
 * etc.), delete the corresponding stub from here and provide
 * the real symbol from a proper implementation file.
 */

#include "netstack/osal.h"

#include <stdint.h>
#include <stddef.h>

/* Handy panic-if-called wrapper for stub debugging. */
#define STUB_TRAP(name) \
    do { osal_panic("rump stub called: " #name); } while (0)

/* ---- Default vtable entries (kern_stub.c would provide but
 *      that file conflicts with kern_ktrace.c) --------------- */

int  nullop(void *arg)                   { (void)arg; return 0; }
int  enxio(void *arg)                    { (void)arg; return 6 /* ENXIO */; }
int  enodev(void *arg)                   { (void)arg; return 19 /* ENODEV */; }
int  eopnotsupp(void *arg)               { (void)arg; return 45 /* EOPNOTSUPP */; }
int  einval(void *arg)                   { (void)arg; return 22 /* EINVAL */; }
int  enosys(void *arg)                   { (void)arg; return 38 /* ENOSYS */; }

/* Device / vnode switch entries with error-returning defaults —
 * standardly provided by conf/{devsw,vnodesw}.c which we don't
 * compile. Fill in as opaque arrays; anything dereferencing will
 * trap on runtime, but the link succeeds. */
struct { int _pad[16]; } devenodev;
struct { int _pad[16]; } ttyvenodev;
int  sys_nosys(void *l, const void *uap, void *retval)
{ (void)l; (void)uap; (void)retval; return 38; }
void spldebug_stop(void)                 { }

/* ---- Kernel globals (data) ---------------------------------- */

int    coherency_unit = 32;      /* PPC dcache line size */
int    maxcpus = 1;
int    maxfiles = 1024;
int    maxlwp = 512;
int    maxproc = 256;
int    tick = 10000;             /* µs per hz-tick (hz=100 → 10ms) */
int    tickadj = 40;             /* NTP adjustment increment */
int    hz_val_pad;               /* placeholder */
long   time_adjtime = 0;

void  *kernel_map = NULL;
void  *kernel_pmap_ptr = NULL;
void  *kmem_arena = NULL;
void  *kmem_va_arena = NULL;
void  *pnbuf_cache = NULL;
void  *uvm = NULL;
void  *fs_filtops = NULL;

/* NetBSD sysctl "kern.version" — kernel-authored string. */
const char version[] = "netstack-amigaos4 rump-stub 0.0\n";
const char copyright[] = "Copyright (c) netstack-amigaos4 contributors\n";

/* uvmexp — massive struct in real NetBSD. Zero-fill: any code
 * reading it gets zero page counts, which is safe for
 * initialization but WILL crash real VM operations. */
struct { char _pad[4096]; } uvmexp;

/* NetBSD declares these as `const int *const` in
 * sys/uvm/uvm_param.h — pointers to fields inside uvmexp. Real
 * uvm_init.c initializes them via &uvmexp.pagemask etc. When we
 * define them as plain ints, PAGE_MASK = *uvmexp_pagemask
 * dereferences the value-as-pointer and faults on 0xFFF. Give
 * them their real pointer type, backed by static ints we
 * initialize to sensible 4 KB values. */
static int _pagemask_backing  = 4095;
static int _pageshift_backing = 12;
static int _pagesize_backing  = 4096;
const int *const uvmexp_pagemask  = &_pagemask_backing;
const int *const uvmexp_pageshift = &_pageshift_backing;
const int *const uvmexp_pagesize  = &_pagesize_backing;

/* Sleep syncobj for our synchronization primitives. */
void *sleep_syncobj;

/* ---- Sleep queue stubs -------------------------------------- */

void sleepq_init(void *sq)                             { (void)sq; }
void sleepq_enqueue(void *sq, void *wchan, const char *wmsg,
                    void *sobj, int cat)
{ (void)sq; (void)wchan; (void)wmsg; (void)sobj; (void)cat; }
int  sleepq_block(int64_t timo, int catch, void *sobj)
{ (void)timo; (void)catch; (void)sobj; return 0; }
void sleepq_changepri(void *lwp, int pri)              { (void)lwp; (void)pri; }
void sleepq_lendpri(void *lwp, int pri)                { (void)lwp; (void)pri; }
void sleepq_unsleep(void *lwp, int cleanup)            { (void)lwp; (void)cleanup; }
void sleepq_wake(void *sq, void *wchan, unsigned int expected, void *mp)
{ (void)sq; (void)wchan; (void)expected; (void)mp; }

/* ---- UVM stubs ---------------------------------------------- */

void  uvm_init(void)                                   { }
void  uvm_init_limits(void *p)                         { (void)p; }
int   uvm_availmem(int cached)                         { (void)cached; return 65536; }
void *uvm_default_mapaddr(void *p, uintptr_t base, size_t sz, int topdown)
{ (void)p; (void)base; (void)sz; (void)topdown; return NULL; }
int   uvm_km_alloc(void *map, size_t sz, size_t align, int flags)
{ (void)map; (void)sz; (void)align; (void)flags; return 0; }
void  uvm_km_free(void *map, uintptr_t addr, size_t sz, int flags)
{ (void)map; (void)addr; (void)sz; (void)flags; }
void *uvm_km_kmem_alloc(void *vmem, size_t sz, int flags)
{ (void)vmem; (void)sz; (void)flags; return NULL; }
void  uvm_km_kmem_free(void *vmem, uintptr_t addr, size_t sz)
{ (void)vmem; (void)addr; (void)sz; }
int   uvm_km_protect(void *map, uintptr_t start, uintptr_t end, int prot)
{ (void)map; (void)start; (void)end; (void)prot; return 0; }
void  uvm_pageout(void *arg)                           { (void)arg; }
int   uvm_wait(const char *wmsg)                       { (void)wmsg; return 0; }
void  uvm_kick_pdaemon(void)                           { }
int   uvm_map_protect(void *map, uintptr_t s, uintptr_t e, int prot, int set_max)
{ (void)map; (void)s; (void)e; (void)prot; (void)set_max; return 0; }
int   uvm_loanbreak(void *page)                        { (void)page; return 0; }
void *uvm_pagealloc_strat(void *obj, uintptr_t off, void *anon, int flags, int strat, int free_list)
{ (void)obj; (void)off; (void)anon; (void)flags; (void)strat; (void)free_list; return NULL; }
void  uvm_pagedeactivate(void *pg)                     { (void)pg; }
void  uvm_pagefree(void *pg)                           { (void)pg; }
void *uvm_pagelookup(void *obj, uintptr_t off)         { (void)obj; (void)off; return NULL; }
void  uvm_pagewire(void *pg)                           { (void)pg; }
void  uvm_pageunwire(void *pg)                         { (void)pg; }
void  uvm_pagewait(void *pg, void *lock, const char *wmsg)
{ (void)pg; (void)lock; (void)wmsg; }
void  uvm_page_unbusy(void **pgs, int npgs)            { (void)pgs; (void)npgs; }
void  uvm_pagelock(void *pg)                           { (void)pg; }
void  uvm_pageunlock(void *pg)                         { (void)pg; }

void  uvmspace_addref(void *vm)                        { (void)vm; }
void  uvmspace_free(void *vm)                          { (void)vm; }
void  uvmspace_init(void *vm, void *pmap, uintptr_t min, uintptr_t max, int topdown)
{ (void)vm; (void)pmap; (void)min; (void)max; (void)topdown; }

/* ---- UBC stubs --------------------------------------------- */

void  ubc_purge(void *uobj)                            { (void)uobj; }
int   ubc_uiomove(void *uobj, void *uio, size_t sz, int advice, int flags)
{ (void)uobj; (void)uio; (void)sz; (void)advice; (void)flags; return 0; }

/* ---- pmap stubs -------------------------------------------- */

int  pmap_clear_modify(void *pg)                       { (void)pg; return 0; }
void pmap_page_protect(void *pg, int prot)             { (void)pg; (void)prot; }
long pmap_resident_count(void *pmap)                   { (void)pmap; return 0; }

/* ---- Property list stubs ---------------------------------- */

int   prop_dictionary_get_bool(void *d, const char *k, int *v)
{ (void)d; (void)k; if (v) *v = 0; return 0; }
void *prop_dictionary_get_keysym(void *d, void *k)     { (void)d; (void)k; return NULL; }
void *prop_dictionary_iterator(void *d)                { (void)d; return NULL; }
const char *prop_dictionary_keysym_value(void *k)      { (void)k; return NULL; }
int   prop_dictionary_set(void *d, const char *k, void *v) { (void)d; (void)k; (void)v; return 1; }
void  prop_kern_init(void)                             { }
void *prop_object_iterator_next(void *iter)            { (void)iter; return NULL; }
void  prop_object_iterator_release(void *iter)         { (void)iter; }
void  prop_object_release(void *obj)                   { (void)obj; }

/* ---- SHA1/SHA256 stubs (used by kernel entropy) ----------- */

void SHA1Init(void *ctx)                               { (void)ctx; }
void SHA1Update(void *ctx, const void *data, unsigned int len)
{ (void)ctx; (void)data; (void)len; }
void SHA1Final(unsigned char *digest, void *ctx)       { (void)digest; (void)ctx; }
void SHA256_Init(void *ctx)                            { (void)ctx; }
void SHA256_Update(void *ctx, const void *data, size_t len)
{ (void)ctx; (void)data; (void)len; }
void SHA256_Final(unsigned char *digest, void *ctx)    { (void)digest; (void)ctx; }

/* ---- entpool (entropy pool) -------------------------------- */

int  entpool_enter(void *p, const void *b, size_t s, int credit)
{ (void)p; (void)b; (void)s; (void)credit; return 0; }
int  entpool_enter_nostir(void *p, const void *b, size_t s)
{ (void)p; (void)b; (void)s; return 0; }
int  entpool_extract(void *p, void *b, size_t s)
{ (void)p; (void)b; (void)s; return 0; }
int  entpool_selftest(void)                            { return 0; }
void entpool_stir(void *p, uint64_t nonce)             { (void)p; (void)nonce; }

/* ---- CPU per-CPU accessors -------------------------------- */

void *cpu_info = NULL;
int   cpu_clkf_intr(void *frame)                       { (void)frame; return 0; }
uintptr_t cpu_clkf_pc(void *frame)                     { (void)frame; return 0; }
int   cpu_clkf_usermode(void *frame)                   { (void)frame; return 0; }
uintptr_t cpu_lwp_pc(void *lwp)                        { (void)lwp; return 0; }

/* ---- Autoconf / device stubs ------------------------------ */

void *device_lookup(void *dv, int unit)                { (void)dv; (void)unit; return NULL; }
void *device_lookup_acquire(void *dv, int unit)        { (void)dv; (void)unit; return NULL; }
void  device_release(void *dev)                        { (void)dev; }
void  config_detach_commit(void *dev)                  { (void)dev; }
void  config_init(void)                                { }

/* ---- NTP time stubs --------------------------------------- */

void ntp_init(void)                                    { }
void ntp_update_second(int64_t *adj, int64_t *newsec)  { (void)adj; (void)newsec; }

/* ---- Misc kernel utility stubs ----------------------------- */

int  membar_exit_needed;   /* deprecated; membar_exit replaced */
void membar_exit(void)     { __asm__ __volatile__ ("sync" ::: "memory"); }

int  splraise(int newipl)                              { (void)newipl; return 0; }
void splx(int ipl)                                     { (void)ipl; }

int  kern_assert(const char *msg, ...)                 { (void)msg; return 0; }
int  copystr(const void *kfaddr, void *kdaddr, size_t len, size_t *done)
{
    const char *s = (const char *)kfaddr;
    char *d = (char *)kdaddr;
    size_t i;
    for (i = 0; i < len && s[i]; i++) d[i] = s[i];
    if (i < len) { d[i] = 0; if (done) *done = i + 1; return 0; }
    if (done) *done = i;
    return 34 /* ENAMETOOLONG */;
}
void hexdump(void *printer, const char *msg, const void *addr, size_t len)
{ (void)printer; (void)msg; (void)addr; (void)len; }
void explicit_memset(void *p, int c, size_t n)
{
    volatile unsigned char *q = p;
    while (n--) *q++ = (unsigned char)c;
}
int  consttime_memequal(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    unsigned char acc = 0;
    while (n--) acc |= (*x++ ^ *y++);
    return acc == 0;
}
int  popcount32(uint32_t v)
{
    v = v - ((v >> 1) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    return (int)((((v + (v >> 4)) & 0xF0F0F0Fu) * 0x01010101u) >> 24);
}
void kheapsort(void *base, size_t nel, size_t width,
               int (*compar)(const void *, const void *), void *tmp)
{ (void)base; (void)nel; (void)width; (void)compar; (void)tmp; }
int  sysctl_basenode_init(void)                        { return 0; }

/* ---- lwp / kobj / syncobj stubs --------------------------- */

void  lwp_unlock_to(void *lwp, void *mp)               { (void)lwp; (void)mp; }
void  syncobj_noowner(void *sobj, void **lp)           { (void)sobj; if (lp) *lp = NULL; }
int   kobj_machdep(void *ko, void *base, size_t sz, int load)
{ (void)ko; (void)base; (void)sz; (void)load; return 0; }
int   kobj_reloc(void *ko, uintptr_t reltab, void *sym, int local, int rela)
{ (void)ko; (void)reltab; (void)sym; (void)local; (void)rela; return 0; }
