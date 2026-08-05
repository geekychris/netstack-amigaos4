/*
 * tests/phase2/test_engine_ping.c — end-to-end scaffold verification.
 *
 * Starts the netstack engine, sends N ping requests through the
 * named request port, verifies each echoes back correctly, stops
 * the engine.
 *
 * Success criteria:
 *   - NetstackEngine_Start returns 0 (process spawned, port up)
 *   - FindPort(NETSTACK_REQUEST_PORT_NAME) resolves
 *   - Each ping RPC returns err=0, payload == sent+1
 *   - NetstackEngine_Stop cleanly tears down
 */

#include "netstack/netstack.h"
#include "netstack/netstack_ipc.h"
#include "netstack/osal.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/exectags.h>
#include <exec/nodes.h>

#define N_PINGS 8

static LONG
send_one_ping(struct MsgPort *engine_port, struct MsgPort *reply,
              uint32_t seq, uint32_t payload,
              uint32_t *out_payload, int32_t *out_err)
{
    struct NetstackReq req = {0};
    req.msg.mn_Node.ln_Type = NT_MESSAGE;
    req.msg.mn_Length       = sizeof(req);
    req.msg.mn_ReplyPort    = reply;
    req.op                  = NETSTACK_OP_PING;
    req.u.ping.seq          = seq;
    req.u.ping.payload      = payload;

    IExec->PutMsg(engine_port, &req.msg);
    (void)IExec->WaitPort(reply);
    struct NetstackReq *r = (struct NetstackReq *)IExec->GetMsg(reply);
    if (!r) return -1;
    *out_payload = r->u.ping.payload;
    *out_err     = r->err;
    return 0;
}

int
main(int argc, char **argv)
{
    (void)argc; (void)argv;
    IDOS->Printf("test_engine_ping: starting engine...\n");
    IDOS->FFlush(IDOS->Output());

    if (NetstackEngine_Start(NULL) != 0) {
        IDOS->Printf("engine start FAILED\n");
        return 20;
    }
    IDOS->Printf("engine up\n");
    IDOS->FFlush(IDOS->Output());

    struct MsgPort *engine_port = IExec->FindPort(NETSTACK_REQUEST_PORT_NAME);
    if (!engine_port) {
        IDOS->Printf("FindPort(\"%s\") returned NULL\n",
                     NETSTACK_REQUEST_PORT_NAME);
        NetstackEngine_Stop();
        return 20;
    }
    IDOS->Printf("found engine port at %p\n", engine_port);
    IDOS->FFlush(IDOS->Output());

    struct MsgPort *reply = (struct MsgPort *)IExec->AllocSysObjectTags(
        ASOT_PORT, ASOPORT_AllocSig, TRUE, TAG_END);
    if (!reply) {
        IDOS->Printf("reply port alloc failed\n");
        NetstackEngine_Stop();
        return 20;
    }

    LONG ok = 1;
    for (int i = 0; i < N_PINGS; i++) {
        uint32_t out_pay = 0;
        int32_t  out_err = 0;
        if (send_one_ping(engine_port, reply,
                          (uint32_t)i, (uint32_t)(i * 10),
                          &out_pay, &out_err) != 0) {
            IDOS->Printf("ping %ld: send failed\n", (LONG)i);
            ok = 0; break;
        }
        LONG want = (LONG)(i * 10 + 1);
        LONG got  = (LONG)out_pay;
        IDOS->Printf("ping %ld: err=%ld payload=%ld (want %ld) %s\n",
                     (LONG)i, (LONG)out_err, got, want,
                     (out_err == NETSTACK_OK && got == want) ? "ok" : "MISMATCH");
        if (out_err != NETSTACK_OK || got != want) ok = 0;
        IDOS->FFlush(IDOS->Output());
    }

    IExec->FreeSysObject(ASOT_PORT, reply);

    IDOS->Printf("stopping engine...\n");
    IDOS->FFlush(IDOS->Output());
    NetstackEngine_Stop();
    IDOS->Printf("test_engine_ping: %s\n", ok ? "PASS" : "FAIL");
    IDOS->FFlush(IDOS->Output());
    return ok ? 0 : 20;
}
