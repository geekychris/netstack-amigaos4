/*
 * netstack/netstack_client.h — thin C API over the engine RPC.
 *
 * Callers link libnetstack_client.a and use these functions
 * directly. Each call performs one round-trip through the
 * netstack.request MsgPort: caller PutMsg's a NetstackReq,
 * WaitPort's on a per-task reply port, GetMsg's the response.
 *
 * The eventual bsdsocket.library shim (Phase 3 continuation)
 * will re-export these with the AmigaOS `LONG socket(LONG,LONG,LONG)`
 * signatures, add per-task errno storage, and wire in
 * WaitSelect/Signal integration. Everything upstream of that shim
 * is already here.
 *
 * Return values: 0 = success (result in *out params); negative =
 * -errno (NETSTACK_E*).
 */

#ifndef NETSTACK_NETSTACK_CLIENT_H
#define NETSTACK_NETSTACK_CLIENT_H

#include <exec/types.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Explicit init/teardown — optional; each RPC lazily initializes
 * on first use. Call these at process start / end if you want
 * to hoist the cost or check the engine is reachable up front.
 *
 * netstack_client_init returns 0 if the engine port is reachable
 * (FindPort resolved). Non-zero if the engine isn't up — caller
 * should try NetstackEngine_Start or bail.
 */
int  netstack_client_init(void);
void netstack_client_shutdown(void);

/*
 * Ping — echoes payload+1 back. Round-trip verification of the
 * engine port. Returns 0 on success.
 */
int  netstack_ping(uint32_t seq, uint32_t payload, uint32_t *out_payload);

/*
 * Socket ops. Currently all return NETSTACK_ENOSYS from the
 * engine (the dispatch table doesn't have real implementations
 * yet — the rump kernel isn't imported). But the RPC path is
 * exercised end-to-end, so once the engine gains a real handler,
 * these become functional with no client-side changes.
 */
int  netstack_socket(int domain, int type, int proto, int *out_sock);
int  netstack_close(int sock);

#ifdef __cplusplus
}
#endif

#endif /* NETSTACK_NETSTACK_CLIENT_H */
