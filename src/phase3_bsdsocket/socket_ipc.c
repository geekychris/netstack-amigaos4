/*
 * phase3_bsdsocket/socket_ipc.c — client-side RPC helpers.
 *
 * Per-task cached reply port, single cached engine port pointer.
 * Callers use netstack_client.h (netstack_socket, netstack_ping,
 * etc.); each function fills in a NetstackReq, calls the private
 * netstack_ipc_call() below, and unpacks the response.
 *
 * Everything IExec-only — safe from both main Process and any
 * spawned Task.
 */

#include "netstack/netstack.h"
#include "netstack/netstack_client.h"
#include "netstack/netstack_ipc.h"
#include "netstack/osal.h"

#include <proto/exec.h>
#include <exec/exectags.h>
#include <exec/semaphores.h>
#include <exec/nodes.h>

/* -------- Cached engine port ---------------------------------- */

/* Engine port doesn't change across the process lifetime once
 * the engine is up. Cached lazily and guarded by a semaphore
 * so the first N callers race safely. */
static struct SignalSemaphore g_engine_port_sem;
static int                    g_engine_port_sem_init;
static struct MsgPort        *g_engine_port;

static struct MsgPort *
ensure_engine_port(void)
{
    if (!g_engine_port_sem_init) {
        /* Racy first-init: OK because caller-of-first must call
         * netstack_client_init before spawning workers, per docs. */
        IExec->InitSemaphore(&g_engine_port_sem);
        g_engine_port_sem_init = 1;
    }
    IExec->ObtainSemaphore(&g_engine_port_sem);
    if (!g_engine_port) {
        g_engine_port = IExec->FindPort(NETSTACK_REQUEST_PORT_NAME);
    }
    struct MsgPort *p = g_engine_port;
    IExec->ReleaseSemaphore(&g_engine_port_sem);
    return p;
}

/* -------- Per-task reply port -------------------------------- */

/*
 * Every task that calls into the client library gets its own
 * reply port. Reusable across RPCs from the same task. Table
 * grows on demand up to REPLY_SLOT_MAX; slots aren't freed
 * (mirroring how osal_timer.c handles per-task IORequests).
 */
#define REPLY_SLOT_MAX 16

struct reply_slot {
    struct Task    *owner;
    struct MsgPort *port;
};

static struct SignalSemaphore g_reply_sem;
static int                    g_reply_sem_init;
static struct reply_slot      g_reply_slots[REPLY_SLOT_MAX];

static struct MsgPort *
ensure_reply_port(void)
{
    struct Task *me = IExec->FindTask(NULL);
    if (!g_reply_sem_init) {
        IExec->InitSemaphore(&g_reply_sem);
        g_reply_sem_init = 1;
    }
    IExec->ObtainSemaphore(&g_reply_sem);
    /* Existing? */
    for (int i = 0; i < REPLY_SLOT_MAX; i++) {
        if (g_reply_slots[i].owner == me) {
            struct MsgPort *p = g_reply_slots[i].port;
            IExec->ReleaseSemaphore(&g_reply_sem);
            return p;
        }
    }
    /* Allocate a new one. */
    for (int i = 0; i < REPLY_SLOT_MAX; i++) {
        if (g_reply_slots[i].owner == NULL) {
            struct MsgPort *p = (struct MsgPort *)IExec->AllocSysObjectTags(
                ASOT_PORT, ASOPORT_AllocSig, TRUE, TAG_END);
            if (!p) { IExec->ReleaseSemaphore(&g_reply_sem); return NULL; }
            g_reply_slots[i].owner = me;
            g_reply_slots[i].port  = p;
            IExec->ReleaseSemaphore(&g_reply_sem);
            return p;
        }
    }
    IExec->ReleaseSemaphore(&g_reply_sem);
    return NULL;   /* out of slots */
}

/* -------- Generic RPC ---------------------------------------- */

/*
 * Send `req` (with .op and .u.* filled in), wait for reply, copy
 * the reply back into `*req` in place. Returns 0 if the message
 * round-tripped; -1 if we couldn't reach the engine.
 *
 * On success, callers inspect req->err for the operation's
 * result (NETSTACK_OK or a negative errno).
 */
static int
netstack_ipc_call(struct NetstackReq *req)
{
    struct MsgPort *engine = ensure_engine_port();
    if (!engine) return -1;
    struct MsgPort *reply = ensure_reply_port();
    if (!reply)  return -1;

    req->msg.mn_Node.ln_Type = NT_MESSAGE;
    req->msg.mn_Length       = sizeof(*req);
    req->msg.mn_ReplyPort    = reply;

    IExec->PutMsg(engine, &req->msg);
    (void)IExec->WaitPort(reply);
    (void)IExec->GetMsg(reply);   /* pops off the port; req was in-place */
    return 0;
}

/* -------- Public API ----------------------------------------- */

int
netstack_client_init(void)
{
    if (!g_engine_port_sem_init) {
        IExec->InitSemaphore(&g_engine_port_sem);
        g_engine_port_sem_init = 1;
    }
    if (!g_reply_sem_init) {
        IExec->InitSemaphore(&g_reply_sem);
        g_reply_sem_init = 1;
    }
    return ensure_engine_port() ? 0 : -1;
}

void
netstack_client_shutdown(void)
{
    /* Free the per-task reply ports we accumulated. */
    if (!g_reply_sem_init) return;
    IExec->ObtainSemaphore(&g_reply_sem);
    for (int i = 0; i < REPLY_SLOT_MAX; i++) {
        if (g_reply_slots[i].port) {
            IExec->FreeSysObject(ASOT_PORT, g_reply_slots[i].port);
            g_reply_slots[i].port  = NULL;
            g_reply_slots[i].owner = NULL;
        }
    }
    IExec->ReleaseSemaphore(&g_reply_sem);
    g_engine_port = NULL;   /* just clears cache; port is owned by engine */
}

int
netstack_ping(uint32_t seq, uint32_t payload, uint32_t *out_payload)
{
    struct NetstackReq req = {0};
    req.op             = NETSTACK_OP_PING;
    req.u.ping.seq     = seq;
    req.u.ping.payload = payload;

    if (netstack_ipc_call(&req) != 0) return -1;
    if (out_payload) *out_payload = req.u.ping.payload;
    return req.err;   /* NETSTACK_OK on success */
}

int
netstack_socket(int domain, int type, int proto, int *out_sock)
{
    struct NetstackReq req = {0};
    req.op                = NETSTACK_OP_SOCKET;
    req.u.socket.domain   = domain;
    req.u.socket.type     = type;
    req.u.socket.proto    = proto;

    if (netstack_ipc_call(&req) != 0) return -1;
    if (out_sock) *out_sock = req.u.socket.sock;
    return req.err;
}

int
netstack_close(int sock)
{
    struct NetstackReq req = {0};
    req.op            = NETSTACK_OP_CLOSE;
    req.u.close.sock  = sock;

    if (netstack_ipc_call(&req) != 0) return -1;
    return req.err;
}
