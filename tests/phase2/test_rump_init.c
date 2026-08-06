/*
 * tests/phase2/test_rump_init.c — trap-code catcher edition.
 *
 * We install SetTaskTrap handlers for every exception class rump
 * could plausibly hit (bus/DSI/ISI/alignment/illegal/priv/FPU).
 * On fault, the handler:
 *   1. Copies the ExceptionContext into a global.
 *   2. Redirects ip → crash_dump().
 *   3. Points SP at a preallocated recovery stack (the fault SP
 *      is likely garbage, which is what "Stackpointer inside/
 *      beyond bounds" implies).
 *   4. Returns 1 so exec resumes execution at ip (crash_dump)
 *      instead of firing the GrimReaper.
 *
 * crash_dump() runs in normal task context (post-trap) and can
 * safely call IDOS->Printf, IDOS->Open, etc. to record every
 * useful register / SPR / stack slice for later analysis.
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/interrupts.h>
#include <exec/tasks.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

unsigned long __stack = 2 * 1024 * 1024;

extern int rump_init(void);
extern int rumpuser_init(int, const void *);
extern int rumpuser_getparam(const char *, void *, size_t);

/* Copy of the exception context, populated in the trap handler
 * and consumed by crash_dump. Also a preallocated recovery
 * stack — crash_dump runs on this instead of the potentially
 * corrupt SP at fault time. */
static struct ExceptionContext g_crash_ctx;
static volatile ULONG g_trap_num_hit;
#define RECOVERY_STACK_SIZE (64 * 1024)
static unsigned char g_recovery_stack[RECOVERY_STACK_SIZE]
    __attribute__((aligned(16)));

/* Forward decl — the resume target we redirect ip to. */
static void crash_dump(void);

/* One trap handler per class — records which class, then routes
 * to the shared post-processing. Called in interrupt-like
 * context, so keep it minimal. */
static ULONG
trap_handler(struct ExceptionContext *ctx, struct ExecBase *sb,
             APTR trapData)
{
    (void)sb;
    g_trap_num_hit = (ULONG)(uintptr_t)trapData;
    /* Copy fixed-size prefix — the FP/vector suffix is huge and
     * we don't need it for a crash report. */
    memcpy(&g_crash_ctx, ctx,
           offsetof(struct ExceptionContext, fpr));

    /* Redirect resume PC to crash_dump. */
    ctx->ip = (uint32)(uintptr_t)crash_dump;
    /* Point SP at the top of our recovery stack (aligned to 16). */
    uint32 top = (uint32)(uintptr_t)(g_recovery_stack + RECOVERY_STACK_SIZE);
    top &= ~15u;
    ctx->gpr[1] = top;

    return 1;   /* "handled" — resume at ip with above SP */
}

static const struct {
    ULONG num;
    const char *name;
} TRAPS[] = {
    { TRAPNUM_BUS_ERROR,              "BUS_ERROR" },
    { TRAPNUM_DATA_SEGMENT_VIOLATION, "DATA_SEG_VIOL" },
    { TRAPNUM_INST_SEGMENT_VIOLATION, "INST_SEG_VIOL" },
    { TRAPNUM_ALIGNMENT,              "ALIGNMENT" },
    { TRAPNUM_ILLEGAL_INSTRUCTION,    "ILLEGAL_INSN" },
    { TRAPNUM_PRIVILEGE_VIOLATION,    "PRIV_VIOL" },
    { TRAPNUM_TRAP,                   "TRAP" },
    { TRAPNUM_FPU,                    "FPU" },
    { TRAPNUM_TRACE,                  "TRACE" },
};

static const char *trap_name(ULONG num)
{
    for (unsigned i = 0; i < sizeof(TRAPS)/sizeof(TRAPS[0]); i++)
        if (TRAPS[i].num == num) return TRAPS[i].name;
    return "UNKNOWN";
}

static void
crash_dump(void)
{
    BPTR f = IDOS->Open("T:trap.log", MODE_NEWFILE);
    if (!f) exit(20);

    IDOS->FPrintf(f, "=== TRAP CAUGHT ===\n");
    IDOS->FPrintf(f, "trap num  = 0x%08lx (%s)\n",
                  g_trap_num_hit, trap_name(g_trap_num_hit));
    IDOS->FPrintf(f, "Flags     = 0x%08lx\n",  g_crash_ctx.Flags);
    IDOS->FPrintf(f, "Traptype  = 0x%08lx\n",  g_crash_ctx.Traptype);
    IDOS->FPrintf(f, "msr       = 0x%08lx\n",  g_crash_ctx.msr);
    IDOS->FPrintf(f, "ip (SRR0) = 0x%08lx  <-- fault PC\n", g_crash_ctx.ip);
    IDOS->FPrintf(f, "lr        = 0x%08lx  <-- caller\n",  g_crash_ctx.lr);
    IDOS->FPrintf(f, "ctr       = 0x%08lx\n",  g_crash_ctx.ctr);
    IDOS->FPrintf(f, "xer       = 0x%08lx\n",  g_crash_ctx.xer);
    IDOS->FPrintf(f, "cr        = 0x%08lx\n",  g_crash_ctx.cr);
    IDOS->FPrintf(f, "dsisr     = 0x%08lx\n",  g_crash_ctx.dsisr);
    IDOS->FPrintf(f, "dar (DEAR)= 0x%08lx  <-- fault data addr\n", g_crash_ctx.dar);

    IDOS->FPrintf(f, "\n-- volatile GPRs (r0..r13) --\n");
    for (int i = 0; i <= 13; i++)
        IDOS->FPrintf(f, "  r%-2d = 0x%08lx\n", i, g_crash_ctx.gpr[i]);
    if (g_crash_ctx.Flags & ECF_FULL_GPRS) {
        IDOS->FPrintf(f, "\n-- nonvol GPRs (r14..r31) --\n");
        for (int i = 14; i < 32; i++)
            IDOS->FPrintf(f, "  r%-2d = 0x%08lx\n", i, g_crash_ctx.gpr[i]);
    } else {
        IDOS->FPrintf(f, "\n(nonvol GPRs not saved — ECF_FULL_GPRS=0)\n");
    }

    /* Fault SP was gpr[1] at trap time. Try to peek at 8 words
     * from there — may reveal caller LR / frame chain. Guard
     * against wild pointers by only trying if it looks
     * plausible (aligned, in first 2 GB). */
    uint32 fault_sp = g_crash_ctx.gpr[1];
    if ((fault_sp & 3) == 0 && fault_sp >= 0x1000 && fault_sp < 0x80000000) {
        IDOS->FPrintf(f, "\n-- 8 words at fault SP=0x%08lx --\n", fault_sp);
        for (int i = 0; i < 8; i++) {
            volatile uint32 *p = (volatile uint32 *)(uintptr_t)(fault_sp + i * 4);
            IDOS->FPrintf(f, "  +%02d: 0x%08lx\n", i * 4, *p);
        }
    } else {
        IDOS->FPrintf(f, "\nfault SP=0x%08lx looks bogus, skipping stack dump\n",
                      fault_sp);
    }

    IDOS->Close(f);
    exit(20);
}

static void
install_all_traps(void)
{
    for (unsigned i = 0; i < sizeof(TRAPS)/sizeof(TRAPS[0]); i++) {
        BOOL ok = IExec->SetTaskTrap(TRAPS[i].num, (APTR)trap_handler,
                                      (APTR)(uintptr_t)TRAPS[i].num);
        IDOS->Printf("  trap[%s] install %s\n", TRAPS[i].name,
                     ok ? "OK" : "FAIL");
    }
}

int
main(int argc, char **argv)
{
    (void)argc; (void)argv;
    IDOS->Printf("test_rump_init: __stack=%lu KB\n", __stack / 1024UL);
    IDOS->Printf("  &main         = %p\n", (void *)main);
    IDOS->Printf("  &rump_init    = %p\n", (void *)rump_init);
    IDOS->Printf("  &rumpuser_init= %p\n", (void *)rumpuser_init);
    IDOS->Printf("  &getparam     = %p\n", (void *)rumpuser_getparam);
    IDOS->Printf("  &crash_dump   = %p\n", (void *)crash_dump);
    IDOS->Printf("  recovery_top  = %p\n",
                 (void *)(g_recovery_stack + RECOVERY_STACK_SIZE));
    IDOS->Printf("Installing trap handlers...\n");
    install_all_traps();
    IDOS->Printf("test_rump_init: calling rump_init()...\n");
    IDOS->FFlush(IDOS->Output());

    int rv = rump_init();

    IDOS->Printf("test_rump_init: rump_init returned %d\n", rv);
    IDOS->FFlush(IDOS->Output());
    return rv ? 20 : 0;
}
