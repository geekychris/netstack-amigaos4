# netstack-amigaos4 — where we are & what to do next

Last update: 2026-08-05, HEAD `f04fc92`.

## Big-picture status

`rump_init()` from a real NetBSD-10 kernel subset now enters our
OSAL, makes ~15 traced hypercalls (mutex/CV/getparam/clock/curlwp),
then deterministically DSI-faults at PC `0x01822DB4` inside a
loaded OS4 system library (below our binary's `~0x7fa7xxxx` load
region). Same PC, same fault every run.

## How to reproduce (the reliable path)

The serial bridge drops mid-transfer on the last partial chunk of
files whose size isn't a multiple of 1024 — do NOT try to
`curl /api/transfer` big binaries. Use `xdftool` directly:

```bash
# 1. build
cd ~/code/claude_world/netstack-amigaos4
./scripts/build.sh build/tests/test_rump_init

# 2. deploy while QEMU is stopped
pkill -9 -f qemu-system-ppc; sleep 2
xdftool ~/AmigaOS4/amigaos4-dev.hdf delete test_rump_init
xdftool ~/AmigaOS4/amigaos4-dev.hdf write \
    ./build/tests/test_rump_init test_rump_init

# 3. start QEMU + devbench (if not already)
~/code/claude_world/amiga_mcp/scripts/start-qemu-os4.sh \
    --gdb --gdb-port 4433 > /tmp/qemu.log 2>&1 &

# 4. wait for guest boot (bridge tick >= 6, silent < 3)
until curl -sf http://localhost:3000/api/status 2>/dev/null | \
      python3 -c 'import sys,json;d=json.load(sys.stdin);\
      hb=d.get("lastHeartbeat");s=d.get("bridgeSilentSec");\
      sys.exit(0 if hb and hb.get("tick",0)>=6 and s and s<3 else 1)' \
      2>/dev/null; do sleep 6; done

# 5. clear old logs + launch
curl -s -X POST http://localhost:3000/api/launch \
     -H 'Content-Type: application/json' \
     -d '{"command":"delete T:trap.log T:tr.out T:trumpinit.log FORCE QUIET"}'
curl -s -X POST http://localhost:3000/api/launch \
     -H 'Content-Type: application/json' \
     -d '{"command":"DH1:test_rump_init >T:tr.out"}'

# 6. read logs (both should exist ~5s after launch)
for p in T:tr.out T:trumpinit.log T:trap.log; do
    echo "--- $p ---"
    curl -s "http://localhost:3000/api/file?path=$p&offset=0&size=16384" | \
        python3 -c 'import sys,json,binascii;h=json.load(sys.stdin).get("hexData","");\
        print(binascii.unhexlify(h).decode("latin-1",errors="replace") if h else "(none)")'
done
```

Full session prompt for a fresh Claude: see `PROMPT.md`.

## The exact fault we're stuck on

```
=== TRAP CAUGHT ===
trap num  = (spurious value — printf bug, ignore)
bad insn  = 0x98090000   = stb r0, 0(r9)     ; store byte 0 to *r9
priv-emulated so far: count=1 last_spr=272 last_kind=1
Traptype  = 0x00000300   = DSI (data storage interrupt)
msr       = 0x0002F030   = supervisor mode (PR=0)
ip (SRR0) = 0x01822DB4   <-- fault PC, inside a system library
lr        = 0x01844BE8   <-- caller, same library
ctr       = 0x00000001   <-- suspiciously = 1
dar       = 0x00000002   <-- fault address (r9 was 2)
```

Rump got past `mi_cpu_init` (mfsprg emulated) and past
`kmem_intr_alloc`'s pagesize math (uvmexp pointer stubs fixed).
The full sequence leading up to the fault is in `T:trumpinit.log`:

```
[rumpuser_init] v=17 hyp=…
[curlwpop] op=0 l=0x…       ; CREATE bootstrap LWP
[curlwpop] op=2 l=0x…       ; SET as current
[getparam] RUMP_THREADS
[getparam] RUMP_VERBOSE
[getparam] _RUMPUSER_NCPU
[malloc] + [mtx_init flags=1]   ; kernel_lock
[malloc] + [cv_init]
[malloc] + [mtx_init flags=2]
[clock_gettime] -> 40.xxx
[malloc] + [cv_init]
[malloc] + [mtx_init flags=2]
[malloc] + [mtx_init flags=3]
[malloc] + [mtx_init flags=1]
[malloc] + [mtx_init flags=2]
[malloc] + [cv_init]
[FAULT HERE — no more osal calls]
```

Between the last `cv_init` and the fault, rump does something
internal that lands in a system library which then dereferences
near-NULL. Prime suspects for what rump does next in this phase:
`kmem_bootstrap`, `pool_subsystem_init`, `mutex_obj_init`, or
`spec_bootstrap`.

## Diagnostic infrastructure we've built (all in `tests/phase2/test_rump_init.c`)

- **SetTaskTrap handlers** for all program-class exceptions.
  On fault → copies ExceptionContext to `g_crash_ctx`, redirects
  `ip` to `crash_dump()` running on a preallocated recovery
  stack, returns 1 so exec resumes there instead of firing the
  GrimReaper. `crash_dump()` writes SRR0/DEAR/LR/all GPRs/8
  stack words to `T:trap.log`.
- **PRIV_VIOL fast-path emulator** — decodes X-form insns at
  fault ip. Handles `mfspr rT, 272..279` (SPRG0..7) by returning
  our shadow `g_sprg[N]` (SPRG0 lazy = `&g_fake_cpu` 4 KB
  writable). Handles `mtspr` symmetrically. Any other priv insn
  falls through to `crash_dump()` so we learn the next culprit.
- **osal_trace(fmt,...)** in `osal_diag.c` — appends a line to
  `T:trumpinit.log`. Cheap; sprinkle liberally.
- **Instrumentation on every rumpuser_ shim** we've hit so far:
  `rumpuser_init`, `getparam`, `malloc`, `mtx_init`, `cv_init`,
  `rw_init`, `curlwpop`, `clock_gettime`.
- **test_trap_emul.c** — 69 KB standalone verifier that runs
  the emulator without rump. Keep for sanity when the big
  binary breaks — it pushes cleanly even when the bridge is
  degraded.

## Next steps (in order)

### 1. Name the library that owns `0x01822DB4`

The library-list dump in `main()` is broken — it printed one
garbled entry then stopped. Options:

- **Rewrite the iteration** with the correct OS4 idiom. LibList
  is a `MinList`, not a `List` — the nodes are `struct Library`
  linked by `lib_Node.ln_Succ`. Also need to lock-then-copy in
  Forbid()ed section. Look at `SDK/Documentation/AutoDocs/`
  entries for `IExec->FindName` and `IExec->AVL_FindPrev`.

- **Or use `IUtility` / `IDOS`** — DOS has `SystemTagList` and
  utility has resource enumeration helpers.

- **Or use the shell**: `curl … launch … 'version FULL … >T:v.out'`
  once for each library base you suspect. Rough approximation
  — bases within a 100 KB range of 0x01822DB4 are candidates.

- **Or check running processes** via `ps` — some libraries are
  loaded per-process and might name the crashing task.

Once named, grep the code path from the last successful
`cv_init` trace forward — the library was called by something
between our shim and the fault.

### 2. Once the library is named — trace how rump got there

Between `[cv_init]` and the fault, rump is executing internal
code. It's probably calling `IExec->FindTask` / `AllocMem` /
`OpenLibrary` from one of our OSAL shims OR from a rump kernel
function that hard-codes an Amiga API (unlikely but possible).

Add trace to `osal_thread_self()`, `osal_thread_create()`,
`osal_mutex_new()`. If none fire, the crash is entirely
within rump-kernel-internal code that then makes ONE library
call. Add trace to `rumpuser_bio` / `rumpuser_iothread` /
`rumpuser_seterrno` — anything that could cross the boundary.

### 3. If library IS Amiga's newlib

`clock_gettime` returned a real value (40s uptime), which
means our timer.device path works. But rump might separately
call `time()` from newlib for jiffies init — and newlib might
call `IIntuition` for timezone which is uninitialized.

Fix: provide our own dummy `time()` that returns 0.

### 4. If library is `intuition.library` or `graphics.library`

Some rump init path may be trying to open a display device.
Unlikely for a rump-kernel-only build, but possible if we've
linked something we shouldn't have.

Grep librump for OpenLibrary calls: `nm build/librump.a | grep
-i "OpenLibrary\|IIntuition\|IGraphics"`.

## Wins from this session (committed to `main`)

```
f04fc92 README: rump-init trace progress, xdftool deploy workflow
43837a2 Traces: curlwp + clock_gettime + xdftool direct-write workaround
fd018fc osal_lock: trace mtx/cv/rw init; get full rump_init early-boot sequence
aba18a0 osal_malloc: trace every allocation call to T:trumpinit.log
ca8e83f uvmexp pointers: fix stub decl → got past kmem_intr_alloc
4fd2309 Emulator VALIDATED: mfsprg trap-emulation works end-to-end
c3f0fca Priv-insn emulator: trap-catch mfsprg / mtsprg to satisfy curcpu()
dd8f946 Trap catcher: pinpoint rump_init crash → privileged mfsprg r9, 0
c3f622c Rump crash bisect: get inside rump_init to first hypercall
5398b01 Rump: NCPU getparam + StackSwap; rump_init gets further but still crashes
```

## Fresh Claude prompt

See `PROMPT.md` at repo root — it briefs a new Claude session on
the layout. Add "Read NEXT_STEPS.md first" at the top of the
opening prompt so it lands on this doc.

## Related memory files

- `~/.claude/projects/-Users-chris-code-claude-world-virte1000/memory/host_side_hdf_editing.md` —
  xdftool workflow (DH1: is DOS3/FFS, safe to edit with QEMU stopped)
- `~/.claude/projects/-Users-chris-code-claude-world-virte1000/memory/gdb_qemu_available.md` —
  `scripts/gdb.sh` attaches to QEMU's PPC gdbstub (limited value for us
  — sam460ex stub returns unreliable register values when halted)
