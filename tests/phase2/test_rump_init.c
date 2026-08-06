/*
 * tests/phase2/test_rump_init.c — try to bring up the rump kernel.
 *
 * We DON'T use StackSwap here. Post-StackSwap Printf never fired,
 * suggesting newlib's per-task state is keyed on the stack range
 * and gets confused after a swap. Instead we set __stack so this
 * binary's initial stack is 2 MB — enough for kernel init.
 *
 * Child threads spawned by rump go through osal_thread_create,
 * which now allocates 512 KB per task (was 16 KB — nowhere near
 * enough for a BSD kernel thread).
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <stddef.h>

/* Amiga executable stack cookie — the shell reads this and sizes
 * the process's stack accordingly. 2 MB. */
unsigned long __stack = 2 * 1024 * 1024;

extern int rump_init(void);
extern int rumpuser_init(int, const void *);
extern int rumpuser_getparam(const char *, void *, size_t);

int
main(int argc, char **argv)
{
    (void)argc; (void)argv;
    IDOS->Printf("test_rump_init: __stack=%lu KB\n", __stack / 1024UL);
    IDOS->Printf("  &main         = %p\n", (void *)main);
    IDOS->Printf("  &rump_init    = %p\n", (void *)rump_init);
    IDOS->Printf("  &rumpuser_init= %p\n", (void *)rumpuser_init);
    IDOS->Printf("  &getparam     = %p\n", (void *)rumpuser_getparam);
    IDOS->Printf("test_rump_init: calling rump_init()...\n");
    IDOS->FFlush(IDOS->Output());

    int rv = rump_init();

    IDOS->Printf("test_rump_init: rump_init returned %d\n", rv);
    IDOS->FFlush(IDOS->Output());
    return rv ? 20 : 0;
}
