/*
 * phase3_bsdsocket/socket_ipc.c — RPC bridge to netstack.process.
 *
 * STATUS: header + skeleton. Real impl marshals a NetstackReq,
 * PutMsg's to the engine's request-port, and blocks on the
 * caller's own reply-port signal.
 */

#include "netstack/netstack.h"
#include "netstack/osal.h"

#include <proto/exec.h>

/* Called by bsdsocket_library.c to send a request to the engine
 * and wait for the reply. Return value is the engine's response
 * code (typically the syscall return); errno set out-of-band. */

int
socket_ipc_call(int op, void *req, size_t req_size,
                void *reply, size_t reply_size)
{
    (void)op; (void)req; (void)req_size;
    (void)reply; (void)reply_size;
    /* TODO:
     *   - Get engine's request port (cached global from library init).
     *   - Allocate a reply port for this task if not already cached.
     *   - Fill NetstackReq, PutMsg, Wait, GetMsg, extract result.
     */
    return -1;
}
