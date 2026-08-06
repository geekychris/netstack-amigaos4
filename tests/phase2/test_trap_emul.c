/*
 * tests/phase2/test_trap_emul.c — standalone verifier for the
 * mfsprg emulator. Doesn't need rump; ~50 KB binary so we can
 * still push it when the bridge is being flaky.
 *
 * Sequence:
 *   1. Install our PRIV_VIOL trap handler.
 *   2. Deliberately execute `mfsprg r9, 0`.
 *   3. If emulator worked → r9 = &g_fake_cpu, execution continues,
 *      we print SUCCESS.
 *   4. If it didn't → we GrimReaper (or crash_dump if any other
 *      trap fires).
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/interrupts.h>
#include <exec/tasks.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Same fake_cpu / g_sprg shadow / emulator core as the big test. */
static uint32 g_sprg[8];
static struct { unsigned char b[4096]; } g_fake_cpu __attribute__((aligned(16)));
static volatile uint32 g_priv_emulated;
static volatile uint32 g_last_bad_insn;
static volatile ULONG  g_trap_num_hit;
static struct ExceptionContext g_crash_ctx;

#define RECOVERY_STACK_SIZE (16 * 1024)
static unsigned char g_recovery_stack[RECOVERY_STACK_SIZE]
    __attribute__((aligned(16)));

static void crash_dump(void);

static int
emulate_priv_insn(struct ExceptionContext *ctx)
{
    uint32 insn = *(volatile uint32 *)(uintptr_t)ctx->ip;
    uint32 opcode = (insn >> 26) & 0x3F;
    if (opcode != 31) return 0;
    uint32 xo     = (insn >> 1)  & 0x3FF;
    uint32 rt     = (insn >> 21) & 0x1F;
    uint32 spr_lo = (insn >> 16) & 0x1F;
    uint32 spr_hi = (insn >> 11) & 0x1F;
    uint32 spr    = (spr_hi << 5) | spr_lo;

    if (xo == 339 && spr >= 272 && spr <= 279) {
        int idx = spr - 272;
        if (idx == 0 && g_sprg[0] == 0)
            g_sprg[0] = (uint32)(uintptr_t)&g_fake_cpu;
        ctx->gpr[rt] = g_sprg[idx];
        ctx->ip += 4;
        g_priv_emulated++;
        return 1;
    }
    if (xo == 467 && spr >= 272 && spr <= 279) {
        g_sprg[spr - 272] = ctx->gpr[rt];
        ctx->ip += 4;
        g_priv_emulated++;
        return 1;
    }
    return 0;
}

static ULONG
trap_handler(struct ExceptionContext *ctx, struct ExecBase *sb, APTR td)
{
    (void)sb;
    ULONG num = (ULONG)(uintptr_t)td;
    if (num == TRAPNUM_PRIVILEGE_VIOLATION && emulate_priv_insn(ctx))
        return 1;

    g_trap_num_hit = num;
    g_last_bad_insn = *(volatile uint32 *)(uintptr_t)ctx->ip;
    memcpy(&g_crash_ctx, ctx, offsetof(struct ExceptionContext, fpr));
    ctx->ip = (uint32)(uintptr_t)crash_dump;
    uint32 top = (uint32)(uintptr_t)(g_recovery_stack + RECOVERY_STACK_SIZE);
    ctx->gpr[1] = top & ~15u;
    return 1;
}

static void
crash_dump(void)
{
    BPTR f = IDOS->Open("T:trapemul.log", MODE_NEWFILE);
    if (!f) exit(20);
    IDOS->FPrintf(f, "TRAP dump:\n");
    IDOS->FPrintf(f, "  trap num  = 0x%08lx\n", g_trap_num_hit);
    IDOS->FPrintf(f, "  bad insn  = 0x%08lx\n", g_last_bad_insn);
    IDOS->FPrintf(f, "  ip        = 0x%08lx\n", g_crash_ctx.ip);
    IDOS->FPrintf(f, "  lr        = 0x%08lx\n", g_crash_ctx.lr);
    IDOS->FPrintf(f, "  dar       = 0x%08lx\n", g_crash_ctx.dar);
    IDOS->FPrintf(f, "  emulated  = %lu\n", g_priv_emulated);
    IDOS->Close(f);
    exit(20);
}

/* Deliberate mfsprg r9, 0 in asm. Wrapped so the compiler
 * doesn't try to optimise it away. */
static uint32 __attribute__((noinline))
do_mfsprg0(void)
{
    register uint32 r9 __asm__("r9");
    __asm__ volatile ("mfsprg %0, 0" : "=r"(r9));
    return r9;
}

int
main(int argc, char **argv)
{
    (void)argc; (void)argv;

    static const struct { ULONG num; const char *name; } TRAPS[] = {
        { TRAPNUM_PRIVILEGE_VIOLATION, "PRIV_VIOL" },
        { TRAPNUM_ILLEGAL_INSTRUCTION, "ILLEGAL"   },
        { TRAPNUM_BUS_ERROR,           "BUS"       },
        { TRAPNUM_DATA_SEGMENT_VIOLATION, "DSEG"   },
        { TRAPNUM_ALIGNMENT,           "ALIGN"     },
    };
    IDOS->Printf("test_trap_emul: installing handlers\n");
    for (unsigned i = 0; i < sizeof(TRAPS)/sizeof(TRAPS[0]); i++) {
        BOOL ok = IExec->SetTaskTrap(TRAPS[i].num, (APTR)trap_handler,
                                      (APTR)(uintptr_t)TRAPS[i].num);
        IDOS->Printf("  %s = %s\n", TRAPS[i].name, ok ? "OK" : "FAIL");
    }

    IDOS->Printf("&g_fake_cpu = %p\n", (void *)&g_fake_cpu);
    IDOS->Printf("Calling mfsprg r9, 0 (will trap + emulate)...\n");
    IDOS->FFlush(IDOS->Output());

    uint32 r9val = do_mfsprg0();

    IDOS->Printf("SUCCESS: mfsprg returned 0x%08lx (expected %p)\n",
                 r9val, (void *)&g_fake_cpu);
    IDOS->Printf("emulated count = %lu\n", g_priv_emulated);
    IDOS->Printf("pass = %s\n",
                 r9val == (uint32)(uintptr_t)&g_fake_cpu ? "YES" : "NO");
    IDOS->FFlush(IDOS->Output());
    return 0;
}
