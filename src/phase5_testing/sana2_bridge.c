/*
 * phase5_testing/sana2_bridge.c — SANA-II ↔ NetDev bridge.
 *
 * STATUS: skeleton. Wraps an existing SANA-II .device (like
 * loopback.device, virtnet.device, virte1000.device) as a
 * NetDev instance. Bridges TX by allocating IOSana2Req +
 * DoIO; RX by pre-queueing CMD_READ requests and feeding
 * completions onto the NetDev RX ring.
 *
 * Necessarily copies memory each way (SANA-II copy hooks
 * demand it). A stopgap so the new stack can talk to legacy
 * drivers until Phase 4 native drivers exist.
 */

#include "netstack/netdev.h"
#include "netstack/osal.h"

#include <proto/exec.h>

struct sana2_bridge {
    struct NetDevIF    iface;
    struct MsgPort    *port;
    /* struct IOSana2Req *req; */   /* TODO */
    STRPTR             device_name;
    ULONG              unit;
};

/* Vector-table method stubs — real impl calls IExec->OpenDevice
 * on the wrapped SANA-II device and translates each NetDev call
 * to a CMD_WRITE / CMD_READ / S2_CONFIGINTERFACE / S2_ONLINE. */

static LONG APICALL b_Open(struct NetDevIF *self, ULONG unit, ULONG flags)
{ (void)self; (void)unit; (void)flags; return -1; }

/* ... TODO all other NetDevIF methods, following the same
 * pattern as phase4_netdev/reference_driver.c ... */

struct NetDevIF *
sana2_bridge_create(STRPTR device_name, ULONG unit, STRPTR bridge_name)
{
    (void)device_name; (void)unit; (void)bridge_name;
    /* TODO:
     *   1. Allocate sana2_bridge, init MsgPort + IOSana2Req.
     *   2. OpenDevice(device_name, unit, req, 0).
     *   3. Fill iface vector table with wrappers around DoIO/SendIO.
     *   4. NetDev_Register(bridge_name, &b->iface).
     */
    (void)b_Open;
    return 0;
}
