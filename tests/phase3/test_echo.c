/*
 * tests/phase3/test_echo.c — full stub-socket loopback echo.
 *
 * Single-task, sequential:
 *
 *   server_fd = socket + bind(port=1234) + listen
 *   client_fd = socket + connect(port=1234)   -- succeeds immediately
 *   accepted  = accept(server_fd)             -- pops pending, pairs
 *   send(client_fd, "hello world!", 12)
 *   recv(accepted, buf, 100)                  -- expect "hello world!"
 *   send(accepted, "pong", 4)
 *   recv(client_fd, buf, 100)                 -- expect "pong"
 *   close all
 *
 * Also verifies:
 *   - connect to unbound port -> ECONNREFUSED
 *   - recv on empty queue -> EAGAIN
 *   - recv after peer close -> len=0, err=OK (EOF)
 */

#include "netstack/netstack.h"
#include "netstack/netstack_client.h"
#include "netstack/netstack_ipc.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

static LONG
expect_eq_str(const char *label, const char *got, int got_len, const char *want)
{
    int wlen = (int)strlen(want);
    if (got_len != wlen) {
        IDOS->Printf("  %s: len mismatch got=%ld want=%ld FAIL\n",
                     label, (LONG)got_len, (LONG)wlen);
        return 0;
    }
    for (int i = 0; i < wlen; i++) {
        if (got[i] != want[i]) {
            IDOS->Printf("  %s: byte %ld got=0x%02lx want=0x%02lx FAIL\n",
                         label, (LONG)i,
                         (unsigned long)(unsigned char)got[i],
                         (unsigned long)(unsigned char)want[i]);
            return 0;
        }
    }
    IDOS->Printf("  %s: \"%s\" ok\n", label, want);
    return 1;
}

int
main(int argc, char **argv)
{
    (void)argc; (void)argv;

    if (NetstackEngine_Start(NULL) != 0) {
        IDOS->Printf("engine start FAILED\n"); return 20;
    }
    if (netstack_client_init() != 0) {
        IDOS->Printf("client init FAILED\n"); NetstackEngine_Stop(); return 20;
    }

    LONG ok = 1;
    int server_fd = -1, client_fd = -1, accepted_fd = -1;

    /* connect-before-listener -> ECONNREFUSED */
    {
        int probe = -1;
        int rv = netstack_socket(2, 1, 0, &probe);
        if (rv != 0) { IDOS->Printf("probe socket FAILED\n"); ok = 0; goto done; }
        rv = netstack_connect(probe, 65535);
        IDOS->Printf("connect-no-listener: rv=%ld (want %ld / ECONNREFUSED)\n",
                     (LONG)rv, (LONG)NETSTACK_ECONNREFUSED);
        if (rv != NETSTACK_ECONNREFUSED) ok = 0;
        netstack_close(probe);
    }

    /* Server: socket + bind + listen */
    if (netstack_socket(2, 1, 0, &server_fd) != 0) {
        IDOS->Printf("server socket FAILED\n"); ok = 0; goto done;
    }
    if (netstack_bind(server_fd, 1234) != 0) {
        IDOS->Printf("bind FAILED\n"); ok = 0; goto done;
    }
    if (netstack_listen(server_fd, 4) != 0) {
        IDOS->Printf("listen FAILED\n"); ok = 0; goto done;
    }
    IDOS->Printf("server ready on port 1234 (fd=%ld)\n", (LONG)server_fd);

    /* accept with no pending -> EAGAIN */
    {
        int nfd = -1;
        int rv = netstack_accept(server_fd, &nfd, NULL);
        IDOS->Printf("accept-empty: rv=%ld (want %ld / EAGAIN)\n",
                     (LONG)rv, (LONG)NETSTACK_EAGAIN);
        if (rv != NETSTACK_EAGAIN) ok = 0;
    }

    /* Client: socket + connect */
    if (netstack_socket(2, 1, 0, &client_fd) != 0) {
        IDOS->Printf("client socket FAILED\n"); ok = 0; goto done;
    }
    if (netstack_connect(client_fd, 1234) != 0) {
        IDOS->Printf("connect FAILED\n"); ok = 0; goto done;
    }
    IDOS->Printf("client connected (fd=%ld)\n", (LONG)client_fd);

    /* accept — now succeeds */
    {
        uint16_t peer = 0;
        int rv = netstack_accept(server_fd, &accepted_fd, &peer);
        IDOS->Printf("accept: rv=%ld new_fd=%ld peer_port=%ld\n",
                     (LONG)rv, (LONG)accepted_fd, (LONG)peer);
        if (rv != NETSTACK_OK || accepted_fd < 0) ok = 0;
    }

    /* recv-empty on the fresh connection -> EAGAIN */
    {
        char buf[16];
        int rv = netstack_recv(accepted_fd, buf, sizeof(buf));
        IDOS->Printf("recv-empty: rv=%ld (want %ld / EAGAIN)\n",
                     (LONG)rv, (LONG)NETSTACK_EAGAIN);
        if (rv != NETSTACK_EAGAIN) ok = 0;
    }

    /* client -> server */
    {
        static const char msg[] = "hello world!";
        int sent = netstack_send(client_fd, msg, (int)strlen(msg));
        IDOS->Printf("send C->S: %ld bytes\n", (LONG)sent);
        if (sent != (int)strlen(msg)) ok = 0;

        char rxbuf[64] = {0};
        int got = netstack_recv(accepted_fd, rxbuf, sizeof(rxbuf) - 1);
        IDOS->Printf("recv S: %ld bytes\n", (LONG)got);
        if (!expect_eq_str("payload C->S", rxbuf, got, msg)) ok = 0;
    }

    /* server -> client */
    {
        static const char msg[] = "pong";
        int sent = netstack_send(accepted_fd, msg, (int)strlen(msg));
        IDOS->Printf("send S->C: %ld bytes\n", (LONG)sent);
        if (sent != (int)strlen(msg)) ok = 0;

        char rxbuf[64] = {0};
        int got = netstack_recv(client_fd, rxbuf, sizeof(rxbuf) - 1);
        IDOS->Printf("recv C: %ld bytes\n", (LONG)got);
        if (!expect_eq_str("payload S->C", rxbuf, got, msg)) ok = 0;
    }

    /* Close the server-accepted side; client should see EOF on
     * next recv (len=0, err=OK). */
    netstack_close(accepted_fd);
    accepted_fd = -1;
    {
        char buf[16];
        int rv = netstack_recv(client_fd, buf, sizeof(buf));
        IDOS->Printf("recv-after-peer-close: rv=%ld (want 0 / EOF)\n", (LONG)rv);
        if (rv != 0) ok = 0;
    }

done:
    if (server_fd    >= 0) netstack_close(server_fd);
    if (client_fd    >= 0) netstack_close(client_fd);
    if (accepted_fd  >= 0) netstack_close(accepted_fd);
    netstack_client_shutdown();
    NetstackEngine_Stop();

    IDOS->Printf("test_echo: %s\n", ok ? "PASS" : "FAIL");
    IDOS->FFlush(IDOS->Output());
    return ok ? 0 : 20;
}
