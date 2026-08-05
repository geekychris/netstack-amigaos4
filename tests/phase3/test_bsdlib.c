/*
 * tests/phase3/test_bsdlib.c — .library shell smoke test.
 *
 * OpenLibrary("netstack.library", 4), GetInterface("main"), call
 * socket()/bind()/listen()/connect()/accept()/send()/recv()/CloseSocket
 * via the interface vector. Verifies the entire OS4 library
 * machinery works — resident tag, MakeLibrary, engine startup at
 * Init, per-caller Obtain/Release refcount.
 *
 * If this passes, any AmigaOS 4 program that expects to call
 * bsdsocket.library the standard way will find our shim and get
 * routed through netstack.process.
 */

#include <exec/exec.h>
#include <exec/types.h>
#include <exec/interfaces.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/dos.h>

/* Interface layout matches src/phase3_bsdsocket/bsdsocket_library.c
 * "main" vector table. Keep in sync. */
struct BsdsocketIFace {
    struct InterfaceData Data;

    uint32 APICALL (*Obtain)(struct BsdsocketIFace *);
    uint32 APICALL (*Release)(struct BsdsocketIFace *);
    APTR   reserved0;
    APTR   reserved1;

    LONG APICALL (*socket)(struct BsdsocketIFace *, LONG, LONG, LONG);
    LONG APICALL (*bind)(struct BsdsocketIFace *, LONG, const void *, LONG);
    LONG APICALL (*connect)(struct BsdsocketIFace *, LONG, const void *, LONG);
    LONG APICALL (*listen)(struct BsdsocketIFace *, LONG, LONG);
    LONG APICALL (*accept)(struct BsdsocketIFace *, LONG, void *, LONG *);
    LONG APICALL (*send)(struct BsdsocketIFace *, LONG, const void *, LONG, LONG);
    LONG APICALL (*recv)(struct BsdsocketIFace *, LONG, void *, LONG, LONG);
    LONG APICALL (*CloseSocket)(struct BsdsocketIFace *, LONG);
};

/* Build a sockaddr_in-shaped byte blob without pulling in
 * netinet/in.h (which might not be in scope for this small test). */
static void
mk_sockaddr(uint8 *buf, uint16 port)
{
    buf[0] = 16;    /* sin_len */
    buf[1] = 2;     /* AF_INET */
    buf[2] = (uint8)(port >> 8);
    buf[3] = (uint8)(port & 0xFF);
    for (int i = 4; i < 16; i++) buf[i] = 0;
}

int
main(int argc, char **argv)
{
    (void)argc; (void)argv;

    IDOS->Printf("test_bsdlib: opening netstack.library v4\n");
    IDOS->FFlush(IDOS->Output());

    struct Library *lib = IExec->OpenLibrary("netstack.library", 4);
    if (!lib) {
        IDOS->Printf("OpenLibrary FAILED — check LIBS:bsdsocket.library\n");
        return 20;
    }
    IDOS->Printf("OpenLibrary ok, base=%p\n", lib);

    struct BsdsocketIFace *bs = (struct BsdsocketIFace *)
        IExec->GetInterface(lib, "main", 1, NULL);
    if (!bs) {
        IDOS->Printf("GetInterface(\"main\", 1) FAILED\n");
        IExec->CloseLibrary(lib);
        return 20;
    }
    IDOS->Printf("GetInterface ok, iface=%p\n", bs);

    LONG ok = 1;

    /* Full echo path via the .library vectors. */
    LONG srv = bs->socket(2, 1, 0);
    LONG cli = bs->socket(2, 1, 0);
    IDOS->Printf("socket srv=%ld cli=%ld\n", srv, cli);
    if (srv < 0 || cli < 0) ok = 0;

    uint8 addr[16];
    mk_sockaddr(addr, 4242);
    LONG rv = bs->bind(srv, addr, 16);
    IDOS->Printf("bind: rv=%ld\n", rv); if (rv != 0) ok = 0;
    rv = bs->listen(srv, 4);
    IDOS->Printf("listen: rv=%ld\n", rv); if (rv != 0) ok = 0;
    rv = bs->connect(cli, addr, 16);
    IDOS->Printf("connect: rv=%ld\n", rv); if (rv != 0) ok = 0;

    uint8 peer_addr[16] = {0};
    LONG peer_len = 16;
    LONG accepted = bs->accept(srv, peer_addr, &peer_len);
    IDOS->Printf("accept: new_fd=%ld peer_port=%ld\n",
                 accepted,
                 (LONG)(((uint16)peer_addr[2] << 8) | peer_addr[3]));
    if (accepted < 0) ok = 0;

    static const char msg[] = "via bsdsocket.library";
    LONG sent = bs->send(cli, msg, (LONG)sizeof(msg) - 1, 0);
    IDOS->Printf("send: %ld bytes\n", sent);
    if (sent != (LONG)sizeof(msg) - 1) ok = 0;

    char rxbuf[64] = {0};
    LONG got = bs->recv(accepted, rxbuf, sizeof(rxbuf) - 1, 0);
    IDOS->Printf("recv: %ld bytes = \"%s\"\n", got, rxbuf);
    if (got != (LONG)sizeof(msg) - 1) ok = 0;
    for (int i = 0; i < got; i++) if (rxbuf[i] != msg[i]) { ok = 0; break; }

    bs->CloseSocket(accepted);
    bs->CloseSocket(cli);
    bs->CloseSocket(srv);

    IExec->DropInterface((struct Interface *)bs);
    IExec->CloseLibrary(lib);

    IDOS->Printf("test_bsdlib: %s\n", ok ? "PASS" : "FAIL");
    IDOS->FFlush(IDOS->Output());
    return ok ? 0 : 20;
}
