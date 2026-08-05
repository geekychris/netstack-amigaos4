# Phase 1 — OS Adaptation Layer (rumpuser)

## Goal

Implement the NetBSD rump kernel's hypercall interface
(`rumpuser_*`, ~50 functions) on top of AmigaOS 4 ExecSG. When
complete, a stock rump kernel binary links against our OSAL and
its self-tests pass.

## Non-goals

- Not the whole NetBSD kernel — only the surface the rump kernel
  needs. NetBSD's rump framework already isolates the interesting
  subsystems.
- Not a general-purpose OS-abstraction library for other projects.
  Every function here is specifically what rump asks for.

## Primary interfaces

The complete list is in NetBSD's `sys/rump/include/rump/rumpuser.h`
(also mirrored as [`include/rumpuser/rumpuser.h`](../include/rumpuser/rumpuser.h)
in this repo). Highlights:

### Memory

```c
int rumpuser_malloc(size_t, int alignment, void **);
void rumpuser_free(void *, size_t);
```

Maps to `IExec->AllocVecTags()` with `MEMF_SHARED` and the
requested alignment. Free via `FreeVec()`. See
[`src/phase1_osal/osal_memory.c`](../src/phase1_osal/osal_memory.c).

### Locking

```c
struct rumpuser_mtx;
struct rumpuser_rw;
struct rumpuser_cv;
void rumpuser_mutex_init(struct rumpuser_mtx **, int flags);
/* ... */
```

- `rumpuser_mtx` → OS4 `struct SignalSemaphore` (recursive-friendly)
  via `AllocSysObjectTags(ASOT_MUTEX, ...)`.
- `rumpuser_rw` → `struct SignalSemaphore` in shared mode
  (`AttemptSemaphoreShared`).
- `rumpuser_cv` → `struct MsgPort` + `AllocSignal()` pair; wait via
  `IExec->Wait()`.

### Threads

```c
int rumpuser_thread_create(void *(*f)(void *), void *arg, ...);
```

Maps to `IExec->CreateTaskTags(ASOT_TASK, ...)`. Each rump thread
gets its own AmigaOS task with a private `tc_UserData` pointing at
the thread's context struct.

### Timers

```c
int rumpuser_clock_gettime(int, int64_t *sec, long *nsec);
int rumpuser_clock_sleep(int, int64_t sec, long nsec);
```

- `clock_gettime` uses `timer.device`'s `TR_GETSYSTIME` (relative
  time) plus a fixed boot-time epoch cached at process start.
- `clock_sleep` uses `TR_ADDREQUEST` on a per-thread reusable
  IORequest.

### I/O primitives

```c
int rumpuser_open(...);
int rumpuser_read(...);
int rumpuser_write(...);
```

Only used by rump for hypercall-side console and configuration
files. Map to `IDOS->Open/Read/Write` on an AmigaOS filesystem —
NOT to the DOS-packet API (which won't work from a non-CLI task).

### Environment / config

```c
int rumpuser_getparam(const char *name, void *buf, size_t buflen);
```

Reads rump kernel tuning knobs from environment. Backed by
`IDOS->GetVar()` reading `ENV:` variables like
`NETSTACK/RUMP_NCPU`, `NETSTACK/RUMP_VERBOSE`, etc.

## Testing strategy

1. **Unit-per-hypercall:** each `rumpuser_*` function has a
   corresponding test binary in `tests/phase1/` that exercises the
   contract (e.g., `test_mutex_recursive` verifies recursive lock
   semantics).
2. **Rump self-tests:** once the OSAL is minimally complete, link
   against `librumpuser` from a NetBSD source snapshot and run
   NetBSD's `rump/tests` suite. This is the definitive proof of
   completeness.
3. **Integration with Phase 2:** the moment Phase 2 can boot a
   rump kernel, any missing hypercall shows up as a link error or
   `rumpuser_panic("nyi")` call.

## Known-hard bits

- **`rumpuser_thread_join` interruption**: joining a thread that
  hasn't exited requires the joiner to block until the target
  returns. AmigaOS 4 doesn't have a direct "wait for task
  termination" primitive; we build one via a per-thread
  `struct MsgPort` that the exiting thread `PutMsg`s to.
- **Priority inheritance**: OS4 signal-semaphores don't do priority
  inheritance. Under heavy contention the rump kernel can prio-invert.
  Practical mitigation: run all rump threads at the same priority,
  live with it.
- **`rumpuser_bio` (block I/O)**: not needed for network-only
  builds. Left unimplemented for now.
- **DMA-suitable memory** for Phase 4: some allocations from within
  the rump kernel end up as driver DMA buffers. We tag those
  requests with a special alignment / MEMF flag and route them to
  a physically-contiguous pool. Requires a rump hook we may need
  to write.

## Current status

**Stub.** All ~50 functions in `include/rumpuser/rumpuser.h` are
declared. `src/phase1_osal/*.c` compiles but every function is a
`panic_nyi()` stub that logs and aborts.

Next unit of work: implement `rumpuser_malloc`/`free` +
`rumpuser_mutex_*` well enough to `dlopen("librumpnet.so", ...)`
and see how far the rump init gets before panicking.
