/*
 * tests/phase3/test_client_rpc.c — client-side helper verification.
 *
 * Uses libnetstack_client.a (netstack_ping, netstack_socket) to
 * exercise the RPC path without going through the raw NetstackReq
 * marshalling. Verifies:
 *
 *   1. netstack_client_init succeeds (engine port found).
 *   2. netstack_ping round-trips correctly (payload+1 back).
 *   3. netstack_socket returns NETSTACK_ENOSYS — expected today
 *      because the engine's socket dispatch is still a stub. This
 *      is the actual proof-of-life: the RPC delivers a response
 *      for an operation the engine doesn't implement yet.
 */

#include "netstack/netstack.h"
#include "netstack/netstack_client.h"
#include "netstack/netstack_ipc.h"

#include <proto/exec.h>
#include <proto/dos.h>

int
main(int argc, char **argv)
{
    (void)argc; (void)argv;
    IDOS->Printf("test_client_rpc: starting engine...\n");
    IDOS->FFlush(IDOS->Output());

    if (NetstackEngine_Start(NULL) != 0) {
        IDOS->Printf("engine start FAILED\n");
        return 20;
    }

    if (netstack_client_init() != 0) {
        IDOS->Printf("netstack_client_init FAILED (engine not reachable)\n");
        NetstackEngine_Stop();
        return 20;
    }
    IDOS->Printf("engine + client ready\n");
    IDOS->FFlush(IDOS->Output());

    LONG ok = 1;

    /* 1. Ping via client. */
    {
        uint32_t got = 0;
        int rv = netstack_ping(0, 42, &got);
        LONG match = (rv == NETSTACK_OK && got == 43);
        IDOS->Printf("ping: rv=%ld got=%lu (want 43) %s\n",
                     (LONG)rv, (unsigned long)got,
                     match ? "ok" : "MISMATCH");
        if (!match) ok = 0;
    }

    /* 2. Socket — must return NETSTACK_ENOSYS today. This is the
     * important test: if we get back anything other than ENOSYS
     * (e.g. -1 for "no round-trip"), the RPC is broken. */
    {
        int sock = -99;
        int rv = netstack_socket(2 /* AF_INET */, 1 /* SOCK_STREAM */, 0, &sock);
        IDOS->Printf("socket: rv=%ld sock=%ld (expected rv=%ld / ENOSYS)\n",
                     (LONG)rv, (LONG)sock, (LONG)NETSTACK_ENOSYS);
        if (rv != NETSTACK_ENOSYS) ok = 0;
    }

    /* Repeat the ping — proves the reply-port cache handles
     * multiple RPCs from the same task. */
    {
        uint32_t got = 0;
        int rv = netstack_ping(1, 100, &got);
        LONG match = (rv == NETSTACK_OK && got == 101);
        IDOS->Printf("ping again: rv=%ld got=%lu (want 101) %s\n",
                     (LONG)rv, (unsigned long)got,
                     match ? "ok" : "MISMATCH");
        if (!match) ok = 0;
    }

    IDOS->Printf("shutting down...\n");
    IDOS->FFlush(IDOS->Output());
    netstack_client_shutdown();
    NetstackEngine_Stop();

    IDOS->Printf("test_client_rpc: %s\n", ok ? "PASS" : "FAIL");
    IDOS->FFlush(IDOS->Output());
    return ok ? 0 : 20;
}
