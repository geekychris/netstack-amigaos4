/*
 * phase2_engine/engine_main.c — netstack.process launcher + main loop.
 *
 * STATUS: skeleton. Boots nothing yet — Phase 1 OSAL is stubs,
 * rump kernel not imported. Wires the pattern so future work
 * plugs into named slots.
 */

#include "netstack/netstack.h"
#include "netstack/osal.h"

#include <proto/exec.h>
#include <exec/exectags.h>
#include <dos/dos.h>          /* SIGBREAKF_CTRL_C */

static struct Task *g_engine_task;
static struct MsgPort *g_request_port;
static volatile BOOL g_shutdown;

static void
engine_process_main(void)
{
    /* 1. Initialize OSAL bookkeeping visible from this task. */
    /* 2. Call rump_init(). NOT YET — rump kernel not imported. */
    /* 3. Bring up loopback interface, sysctl, etc. */
    /* 4. Signal parent we're ready. */
    /* 5. Enter dispatch loop. */

    g_request_port = (struct MsgPort *)IExec->AllocSysObjectTags(
        ASOT_PORT, ASOPORT_AllocSig, TRUE, TAG_END);
    if (!g_request_port) {
        osal_log("engine: request-port alloc failed");
        return;
    }

    ULONG port_sig = 1UL << g_request_port->mp_SigBit;

    /* TODO: signal parent via a supplied signal bit. */

    while (!g_shutdown) {
        ULONG sigs = IExec->Wait(port_sig | SIGBREAKF_CTRL_C);
        if (sigs & SIGBREAKF_CTRL_C) break;
        if (sigs & port_sig) {
            struct Message *m;
            while ((m = IExec->GetMsg(g_request_port)) != NULL) {
                /* TODO: dispatch NetstackReq to rump. */
                IExec->ReplyMsg(m);
            }
        }
    }

    IExec->FreeSysObject(ASOT_PORT, g_request_port);
    g_request_port = NULL;
}

LONG
NetstackEngine_Start(struct NetstackConfig *cfg)
{
    (void)cfg;
    if (g_engine_task) return -1;   /* already up */
    g_shutdown = FALSE;

    g_engine_task = (struct Task *)IExec->CreateTaskTags(
        "netstack.process", 5,
        engine_process_main, 65536,
        TAG_END);
    if (!g_engine_task) return -1;

    /* TODO: wait for READY signal from the process before returning. */
    return 0;
}

LONG
NetstackEngine_Stop(void)
{
    if (!g_engine_task) return 0;
    g_shutdown = TRUE;
    IExec->Signal(g_engine_task, SIGBREAKF_CTRL_C);
    /* TODO: wait for the process to exit cleanly. */
    g_engine_task = NULL;
    return 0;
}

BOOL
NetstackEngine_IsRunning(void)
{
    return g_engine_task != NULL;
}
