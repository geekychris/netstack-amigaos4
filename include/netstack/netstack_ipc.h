/*
 * netstack/netstack_ipc.h — request-message ABI between
 * bsdsocket.library / any native client and netstack.process.
 *
 * Public because Phase 3's bsdsocket.library and Phase 4 test
 * harnesses both need it. Kept in a separate header from
 * netstack.h so that netstack.library consumers don't have to
 * know about the underlying transport (they call methods on
 * NetstackIFace instead).
 *
 * All requests share the leading struct Message so they can be
 * PutMsg'd/GetMsg'd/ReplyMsg'd through a MsgPort. The union
 * carries op-specific args + results.
 */

#ifndef NETSTACK_NETSTACK_IPC_H
#define NETSTACK_NETSTACK_IPC_H

#include <exec/types.h>
#include <exec/ports.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Named port the engine registers. Clients FindPort() this. */
#define NETSTACK_REQUEST_PORT_NAME  "netstack.request"

/* Op codes. Bump the range when adding categories. */
#define NETSTACK_OP_PING       0x0001  /* echo-back — scaffold only */
#define NETSTACK_OP_SHUTDOWN   0x0002  /* engine graceful stop */

/* Socket ops (Phase 3, not implemented yet) */
#define NETSTACK_OP_SOCKET     0x0100
#define NETSTACK_OP_BIND       0x0101
#define NETSTACK_OP_CONNECT    0x0102
#define NETSTACK_OP_LISTEN     0x0103
#define NETSTACK_OP_ACCEPT     0x0104
#define NETSTACK_OP_SEND       0x0105
#define NETSTACK_OP_RECV       0x0106
#define NETSTACK_OP_CLOSE      0x0107

/* Error codes (subset of BSD errno enough for our stubs). */
#define NETSTACK_OK            0
#define NETSTACK_ENOSYS       (-38)
#define NETSTACK_EBADF        (-9)
#define NETSTACK_EAGAIN       (-11)
#define NETSTACK_EINVAL       (-22)
#define NETSTACK_EADDRINUSE   (-48)
#define NETSTACK_ECONNREFUSED (-61)
#define NETSTACK_ENOTCONN     (-57)
#define NETSTACK_EMSGSIZE     (-40)

/* Max in-band payload for send/recv. Larger transfers must chunk. */
#define NETSTACK_MAX_PAYLOAD  1024

struct NetstackReq {
    struct Message  msg;      /* mn_ReplyPort set by caller */
    uint16          op;       /* NETSTACK_OP_* */
    int16           _pad0;
    int32           err;      /* NETSTACK_OK or a negative errno */

    union {
        /* NETSTACK_OP_PING */
        struct {
            uint32 seq;       /* caller sets; engine echoes */
            uint32 payload;   /* caller sets; engine returns payload+1 */
        } ping;

        /* NETSTACK_OP_SHUTDOWN — no args */

        /* NETSTACK_OP_SOCKET */
        struct {
            int32 domain;
            int32 type;
            int32 proto;
            int32 sock;       /* out: fd on success */
        } socket;

        /* NETSTACK_OP_CLOSE */
        struct {
            int32 sock;
        } close;

        /* NETSTACK_OP_BIND — stub layer only cares about port. */
        struct {
            int32  sock;
            uint16 port;
            uint16 _pad;
        } bind;

        /* NETSTACK_OP_LISTEN */
        struct {
            int32 sock;
            int32 backlog;   /* ignored in stub (fixed) */
        } listen;

        /* NETSTACK_OP_CONNECT */
        struct {
            int32  sock;
            uint16 port;
            uint16 _pad;
        } connect;

        /* NETSTACK_OP_ACCEPT */
        struct {
            int32  sock;
            int32  new_sock;   /* out */
            uint16 peer_port;  /* out (stub: always the listen port) */
            uint16 _pad;
        } accept;

        /* NETSTACK_OP_SEND — payload inline for stub simplicity. */
        struct {
            int32  sock;
            int32  len_in;     /* bytes in u.send.data */
            int32  len_out;    /* bytes actually accepted */
            uint8  data[NETSTACK_MAX_PAYLOAD];
        } send;

        /* NETSTACK_OP_RECV — same shape, engine fills data + len_out. */
        struct {
            int32  sock;
            int32  len_in;     /* max bytes to read */
            int32  len_out;    /* actually read */
            uint8  data[NETSTACK_MAX_PAYLOAD];
        } recv;

        /* Padding so future additions don't change size unexpectedly. */
        uint8 raw[NETSTACK_MAX_PAYLOAD + 64];
    } u;
};

#ifdef __cplusplus
}
#endif

#endif /* NETSTACK_NETSTACK_IPC_H */
