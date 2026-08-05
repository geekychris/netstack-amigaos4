/*
 * tests/phase2/test_rump_init.c — try to bring up the rump kernel.
 *
 * Rump kernel init uses way more stack than the OS4 shell's
 * default 64 KB. Allocate a big private stack + StackSwap into
 * it before calling rump_init(). Swap back before returning.
 */

#include <exec/tasks.h>
#include <proto/exec.h>
#include <proto/dos.h>

#define BIG_STACK  (1024 * 1024)     /* 1 MB */

extern int rump_init(void);

/* Called under our big stack. */
static int g_rump_result;
static void
rump_bootstrap(void)
{
    g_rump_result = rump_init();
}

int
main(int argc, char **argv)
{
    (void)argc; (void)argv;
    IDOS->Printf("test_rump_init: allocating %ld KB stack\n",
                 (long)(BIG_STACK / 1024));
    IDOS->FFlush(IDOS->Output());

    void *stack_lower = IExec->AllocVecTags(BIG_STACK,
        AVT_Type,           MEMF_PRIVATE,
        AVT_ClearWithValue, 0,
        TAG_END);
    if (!stack_lower) {
        IDOS->Printf("stack alloc FAILED\n");
        return 20;
    }

    struct StackSwapStruct sss;
    sss.stk_Lower   = stack_lower;
    sss.stk_Upper   = (unsigned long)stack_lower + BIG_STACK;
    sss.stk_Pointer = (APTR)sss.stk_Upper;

    IDOS->Printf("test_rump_init: StackSwap → calling rump_init()...\n");
    IDOS->FFlush(IDOS->Output());

    /* OS4 StackSwap: swap in, run, swap back. */
    IExec->StackSwap(&sss);
    rump_bootstrap();
    IExec->StackSwap(&sss);

    IExec->FreeVec(stack_lower);

    IDOS->Printf("test_rump_init: rump_init returned %ld\n", (long)g_rump_result);
    IDOS->FFlush(IDOS->Output());
    return g_rump_result ? 20 : 0;
}
