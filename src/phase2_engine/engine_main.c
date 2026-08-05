/*
 * phase2_engine/engine_main.c — netstack.process launcher + main loop.
 *
 * Boots netstack.process, registers a named MsgPort so clients
 * can FindPort("netstack.request"), dispatches incoming
 * NetstackReq messages via a small switch, ReplyMsg's each.
 *
 * The rump kernel isn't imported yet, so the dispatch table is
 * intentionally tiny: NETSTACK_OP_PING (echo-back for scaffold
 * verification) and NETSTACK_OP_SHUTDOWN. Everything else
 * returns NETSTACK_ENOSYS.
 */

#include "netstack/netstack.h"
#include "netstack/netstack_ipc.h"
#include "netstack/osal.h"

#include <proto/exec.h>
#include <exec/exectags.h>
#include <exec/nodes.h>
#include <dos/dos.h>              /* SIGBREAKF_CTRL_C */

/* -------- Shared launcher/engine state --------------------------- */

static struct Task    *g_engine_task;
static struct Task    *g_launcher_task;
static ULONG           g_launcher_ready_mask;
static ULONG           g_launcher_stopped_mask;
static volatile BOOL   g_engine_ready;
static volatile BOOL   g_engine_shutdown;
static struct MsgPort *g_request_port;

/* -------- Dispatch --------------------------------------------- */

static void
dispatch_ping(struct NetstackReq *r)
{
    /* Echo seq back, bump payload so caller can verify we saw it. */
    r->u.ping.payload += 1;
    r->err = NETSTACK_OK;
}

static void
dispatch_shutdown(struct NetstackReq *r)
{
    r->err = NETSTACK_OK;
    g_engine_shutdown = TRUE;
    /* Reply is sent by the caller loop AFTER we set shutdown, so
     * the caller unblocks first, then we tear down. */
}

/* fdtable.c */
extern void fdtable_dispatch_socket(struct NetstackReq *);
extern void fdtable_dispatch_close(struct NetstackReq *);
extern void fdtable_dispatch_bind(struct NetstackReq *);
extern void fdtable_dispatch_listen(struct NetstackReq *);
extern void fdtable_dispatch_connect(struct NetstackReq *);
extern void fdtable_dispatch_accept(struct NetstackReq *);
extern void fdtable_dispatch_send(struct NetstackReq *);
extern void fdtable_dispatch_recv(struct NetstackReq *);

static void
dispatch_request(struct NetstackReq *r)
{
    switch (r->op) {
        case NETSTACK_OP_PING:     dispatch_ping(r);          break;
        case NETSTACK_OP_SHUTDOWN: dispatch_shutdown(r);      break;
        case NETSTACK_OP_SOCKET:   fdtable_dispatch_socket(r);  break;
        case NETSTACK_OP_CLOSE:    fdtable_dispatch_close(r);   break;
        case NETSTACK_OP_BIND:     fdtable_dispatch_bind(r);    break;
        case NETSTACK_OP_LISTEN:   fdtable_dispatch_listen(r);  break;
        case NETSTACK_OP_CONNECT:  fdtable_dispatch_connect(r); break;
        case NETSTACK_OP_ACCEPT:   fdtable_dispatch_accept(r);  break;
        case NETSTACK_OP_SEND:     fdtable_dispatch_send(r);    break;
        case NETSTACK_OP_RECV:     fdtable_dispatch_recv(r);    break;
        default:                   r->err = NETSTACK_ENOSYS;    break;
    }
}

/* -------- Process main ---------------------------------------- */

static void
engine_process_main(void)
{
    /* Init port before signalling ready — a caller that races us
     * calling FindPort should either see the port or nothing, never
     * an in-flight-init state. */
    g_request_port = (struct MsgPort *)IExec->AllocSysObjectTags(
        ASOT_PORT,
        ASOPORT_AllocSig, TRUE,
        ASOPORT_Name,     NETSTACK_REQUEST_PORT_NAME,
        ASOPORT_Pri,      0,
        ASOPORT_Public,   TRUE,
        TAG_END);
    if (!g_request_port) {
        osal_log("netstack.process: request port alloc FAILED");
        /* Still need to unblock launcher — but signal FAIL by
         * leaving g_engine_ready = FALSE. Launcher polls it. */
        IExec->Signal(g_launcher_task, g_launcher_ready_mask);
        return;
    }

    g_engine_ready    = TRUE;
    g_engine_shutdown = FALSE;
    IExec->Signal(g_launcher_task, g_launcher_ready_mask);

    ULONG port_sig = 1UL << g_request_port->mp_SigBit;
    ULONG wait_mask = port_sig | SIGBREAKF_CTRL_C;

    while (!g_engine_shutdown) {
        ULONG got = IExec->Wait(wait_mask);
        if (got & SIGBREAKF_CTRL_C) break;
        if (got & port_sig) {
            struct NetstackReq *r;
            while ((r = (struct NetstackReq *)IExec->GetMsg(g_request_port)) != NULL) {
                dispatch_request(r);
                IExec->ReplyMsg((struct Message *)r);
            }
        }
    }

    /* Tear-down. */
    IExec->FreeSysObject(ASOT_PORT, g_request_port);
    g_request_port = NULL;
    g_engine_ready = FALSE;
    IExec->Signal(g_launcher_task, g_launcher_stopped_mask);
}

/* -------- Public API ------------------------------------------ */

LONG
NetstackEngine_Start(struct NetstackConfig *cfg)
{
    if (g_engine_task) return -1;   /* already up */

    g_launcher_task = IExec->FindTask(NULL);
    LONG ready_bit   = IExec->AllocSignal(-1);
    LONG stopped_bit = IExec->AllocSignal(-1);
    if (ready_bit < 0 || stopped_bit < 0) {
        if (ready_bit   >= 0) IExec->FreeSignal(ready_bit);
        if (stopped_bit >= 0) IExec->FreeSignal(stopped_bit);
        return -1;
    }
    g_launcher_ready_mask   = 1UL << ready_bit;
    g_launcher_stopped_mask = 1UL << stopped_bit;
    g_engine_ready = FALSE;

    ULONG priority = cfg && cfg->priority ? cfg->priority : 5;

    IExec->Forbid();
    g_engine_task = (struct Task *)IExec->CreateTaskTags(
        "netstack.process", priority,
        engine_process_main, 65536,
        TAG_END);
    IExec->Permit();
    if (!g_engine_task) {
        IExec->FreeSignal(ready_bit);
        IExec->FreeSignal(stopped_bit);
        return -1;
    }

    /* Wait for READY (or FAIL — engine leaves g_engine_ready
     * FALSE and still signals). */
    (void)IExec->Wait(g_launcher_ready_mask);
    if (!g_engine_ready) {
        /* Engine bailed out. Task is already gone. */
        IExec->FreeSignal(ready_bit);
        IExec->FreeSignal(stopped_bit);
        g_engine_task = NULL;
        return -1;
    }
    return 0;
}

LONG
NetstackEngine_Stop(void)
{
    if (!g_engine_task) return 0;

    /* If we have a request port, send OP_SHUTDOWN. Otherwise fall
     * back to CTRL_C on the task. */
    if (g_request_port) {
        struct MsgPort *reply = (struct MsgPort *)IExec->AllocSysObjectTags(
            ASOT_PORT, ASOPORT_AllocSig, TRUE, TAG_END);
        if (reply) {
            struct NetstackReq req = {0};
            req.msg.mn_Node.ln_Type = NT_MESSAGE;
            req.msg.mn_Length       = sizeof(req);
            req.msg.mn_ReplyPort    = reply;
            req.op                  = NETSTACK_OP_SHUTDOWN;
            IExec->PutMsg(g_request_port, &req.msg);
            (void)IExec->WaitPort(reply);
            (void)IExec->GetMsg(reply);
            IExec->FreeSysObject(ASOT_PORT, reply);
        } else {
            IExec->Signal(g_engine_task, SIGBREAKF_CTRL_C);
        }
    } else {
        IExec->Signal(g_engine_task, SIGBREAKF_CTRL_C);
    }

    /* Wait for the engine to signal it's stopped. */
    (void)IExec->Wait(g_launcher_stopped_mask);

    /* Free the signals we allocated in Start. */
    /* Sig bit numbers aren't stored anywhere directly; recompute
     * from masks. */
    for (int b = 0; b < 32; b++) {
        if (g_launcher_ready_mask   == (1UL << b)) IExec->FreeSignal(b);
        if (g_launcher_stopped_mask == (1UL << b)) IExec->FreeSignal(b);
    }
    g_launcher_ready_mask = g_launcher_stopped_mask = 0;
    g_engine_task = NULL;
    return 0;
}

BOOL
NetstackEngine_IsRunning(void)
{
    return g_engine_task != NULL && g_engine_ready;
}
