/*
 * tests/phase2/test_rump_init.c — try to bring up the rump kernel.
 *
 * Simply calls rump_init() and reports the return code.
 * If this LINKS (even without running yet), that's a huge
 * milestone — proves librump.a + libnetstack_osal.a resolve
 * the actual call path from OS4 entry to the rump kernel core.
 *
 * We expect the link to expose which unresolved symbols are
 * ACTUALLY on the code path from main→rump_init→everything
 * it transitively needs. That subset is far smaller than the
 * ~144 total unresolved in the archives.
 */

#include <proto/exec.h>
#include <proto/dos.h>

/* rump kernel entry — declared in vendor/netbsd-rump/sys/rump/
 * include/rump/rump.h but pulling that in here would drag in a
 * lot of NetBSD-flavored headers. Just prototype it directly. */
extern int rump_init(void);

int
main(int argc, char **argv)
{
    (void)argc; (void)argv;
    IDOS->Printf("test_rump_init: calling rump_init()...\n");
    IDOS->FFlush(IDOS->Output());

    int rv = rump_init();

    IDOS->Printf("test_rump_init: rump_init returned %ld\n", (long)rv);
    IDOS->FFlush(IDOS->Output());
    return rv ? 20 : 0;
}
