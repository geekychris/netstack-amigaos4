# Rump kernel build probe — findings

Attempts at compiling one rump kernel source file
(`sys/kern/kern_mutex.c`) against our cross-toolchain
(`walkero/amigagccondocker:os4-gcc11-arm64`) to validate the
import + build recipe.

**Current state: kern_mutex.c compiles cleanly.** Produces a
6.4 KB PPC object with all the `mutex_*` public symbols defined
(`mutex_init`, `mutex_destroy`, `mutex_tryenter`,
`mutex_spin_enter`, ...). Unresolved references (`U`) are our
OSAL binding TODO: `kpreempt_disable/enable`, `membar_*`,
`lockdebug_*`, `_lock_cas`, etc.

## The compile recipe

Captured as the `rump-probe` Makefile target. Key ingredients:

```make
RUMP_CFLAGS = -mcrt=newlib -mhard-float -O2 -mcpu=440 \
              -D__PPC__ -ffreestanding -fno-strict-aliasing \
              -Wno-format-zero-length -Wno-pointer-sign \
              -imacros $(RUMP_ROOT)/sys/rump/include/opt/opt_rumpkernel.h \
              -I$(RUMP_ROOT)/sys \
              -I$(RUMP_ROOT)/sys/rump/include \
              -I$(RUMP_ROOT)/common/include \
              -I./include -I./include/netstack -I./include/rumpuser
```

Run:
```sh
./scripts/build.sh rump-probe
```

## What each iteration unlocked

| Error                                              | Fix                                                                |
|----------------------------------------------------|--------------------------------------------------------------------|
| `machine/cdefs.h: No such file`                    | Import `sys/arch/powerpc/include`, symlink `sys/machine` → `arch/powerpc/include` |
| `powerpc/int_types.h: No such file`                | Symlink `sys/powerpc` → `arch/powerpc/include` (NetBSD build does both) |
| `lib/libkern/libkern.h: No such file`              | Import `sys/lib/libkern`                                           |
| `uvm/uvm_param.h: No such file`                    | Import `sys/uvm` (headers only, for size constants)                |
| `#error unknown PPC variant`, `MIN/MAX_PAGE_SIZE`  | `-imacros vendor/netbsd-rump/sys/rump/include/opt/opt_rumpkernel.h` — rump ships its own root config |
| `prop/plistref.h: No such file`                    | Import `common/include` (property list headers etc.)               |
| `dev/lockstat.h: No such file`                     | Import `sys/dev` (headers only; 110 MB, biggest single subtree)    |

The `-imacros opt_rumpkernel.h` line was the pivotal find —
it turns out rump ships its own root config header that pins
down MULTIPROCESSOR, MAXUSERS, INET/INET6/GATEWAY, MPLS, ALTQ,
etc. Reading `sys/rump/Makefile.rump` showed rump's build uses
that same file the same way. So we don't need to invent
`opt_*.h` files ourselves — just point at rump's.

## Symbols in the compiled object

```
Defined (T = text, D = data, R = rodata):
  mutex_init, _mutex_init, mutex_destroy, mutex_owner, mutex_owned,
  mutex_tryenter, mutex_ownable, mutex_owner_running,
  mutex_vector_enter, mutex_vector_exit, mutex_spin_enter,
  mutex_spin_exit, mutex_spin_retry
  mutex_adaptive_lockops (D), mutex_spin_lockops (D), mutex_syncobj (D)

Undefined (U — Phase 2 OSAL bindings will resolve):
  kpreempt, kpreempt_disable, kpreempt_enable
  membar_acquire, membar_consumer, membar_enter, membar_release
  _lock_cas, lockdebug_abort, nullop
  ...
```

## What this unblocks

- Any other `sys/kern/*.c` file that doesn't drag in
  additional missing subtrees can now be attempted with the
  same recipe. Expect most kern locking / synch primitives
  (`kern_condvar.c`, `kern_rwlock.c`, `kern_lwp.c`) to hit
  the same header chain.
- Phase 2 work now has a concrete list of unresolved OSAL
  symbols to implement per file, rather than guessing what
  rump expects.

## Realistic timing update

Original roadmap estimate for Phase 1 was 4-6 weeks. The
imports + this probe took a couple of sessions of a few hours
each. That still tracks the estimate; header plumbing was the
easy part. The bulk of Phase 1 is:

1. **Implement the ~50 `rumpuser_*` hypercalls fully** (in
   progress in `src/phase1_osal/`, mostly stubs today).
2. **Provide the kernel-side symbols** the compiled objects
   depend on (`membar_*`, `_lock_cas`, `kpreempt_*`, etc.) —
   either from more imported source (`sys/kern/subr_*.c`,
   `sys/arch/powerpc/powerpc/lock_*.S`) or from tiny OSAL
   bridges.
3. **Get `rump_init()` to actually run**, which will be the
   first real Phase 2 milestone.

## The dead ends we can now avoid

Path we DIDN'T need to take:
- Building NetBSD on a Linux host with `build.sh -m amd64
  tools kernel=RUMP` and cargo-culting the generated `opt_*.h`
  files. `opt_rumpkernel.h` is already in the source and
  contains everything we need.
- Hand-writing an `opt_*.h` forest. Not needed once we knew
  to use `-imacros opt_rumpkernel.h`.
