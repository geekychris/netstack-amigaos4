/*
 * phase4_netdev/reference_driver.c — Intel e1000e reference driver
 * against the NetDev interface.
 *
 * STATUS: skeleton. Vector table with all methods returning -1.
 * Real work: port the RX/TX ring, IRQ handler, and register
 * layout from the SANA-II virte1000.device
 * (github.com/geekychris/virte1000-amigaos4) but reshape the
 * data path to use NetDev's zero-copy rings instead of SANA-II
 * copy hooks.
 */

#include "netstack/netdev.h"
#include "netstack/osal.h"

/* -------- Vector table stubs ---------------------------------- */

static LONG APICALL e_Open(struct NetDevIF *self, ULONG unit, ULONG flags)
{ (void)self; (void)unit; (void)flags; return -1; }

static void APICALL e_Close(struct NetDevIF *self)
{ (void)self; }

static void APICALL e_GetCapabilities(struct NetDevIF *self,
                                      struct NetDevCapabilities *out)
{
    (void)self;
    if (!out) return;
    out->flags          = 0;
    out->rx_queues      = 1;
    out->tx_queues      = 1;
    out->max_frame_size = 1518;
    out->link_speed_bps = 1000000000ULL;
    for (int i = 0; i < 6; i++) out->factory_mac[i] = 0;
}

static LONG APICALL e_AttachRings(struct NetDevIF *self, struct NetDevRings *r)
{ (void)self; (void)r; return -1; }

static void APICALL e_DetachRings(struct NetDevIF *self)
{ (void)self; }

static struct NetDevBuf *APICALL e_AllocBuf(struct NetDevIF *self, ULONG size)
{ (void)self; (void)size; return 0; }

static void APICALL e_FreeBuf(struct NetDevIF *self, struct NetDevBuf *b)
{ (void)self; (void)b; }

static LONG APICALL e_Kick(struct NetDevIF *self, ULONG queue)
{ (void)self; (void)queue; return -1; }

static ULONG APICALL e_Poll(struct NetDevIF *self, ULONG queue,
                            struct NetDevBuf **out, ULONG max)
{ (void)self; (void)queue; (void)out; (void)max; return 0; }

static LONG APICALL e_RegisterKick(struct NetDevIF *self,
                                   void (*cb)(APTR), APTR ctx)
{ (void)self; (void)cb; (void)ctx; return -1; }

static LONG APICALL e_SetMac(struct NetDevIF *self, const UBYTE *m)
{ (void)self; (void)m; return -1; }

static LONG APICALL e_SetLinkState(struct NetDevIF *self, BOOL up)
{ (void)self; (void)up; return -1; }

static LONG APICALL e_SetMulticast(struct NetDevIF *self, const UBYTE *m, BOOL add)
{ (void)self; (void)m; (void)add; return -1; }

static LONG APICALL e_SetPromisc(struct NetDevIF *self, BOOL on)
{ (void)self; (void)on; return -1; }

static void APICALL e_GetStats(struct NetDevIF *self, struct NetDevStats *s)
{
    (void)self;
    if (s) for (size_t i = 0; i < sizeof(*s); i++) ((char *)s)[i] = 0;
}

/* -------- Instance & registration ----------------------------- */

/* NOTE: this is a placeholder — real driver would fill Data via
 * MakeInterface with a full struct InterfaceData. */
static struct NetDevIF g_e1000e_iface = {
    .Data            = { 0 },
    .Open            = e_Open,
    .Close           = e_Close,
    .GetCapabilities = e_GetCapabilities,
    .AttachRings     = e_AttachRings,
    .DetachRings     = e_DetachRings,
    .AllocBuf        = e_AllocBuf,
    .FreeBuf         = e_FreeBuf,
    .Kick            = e_Kick,
    .Poll            = e_Poll,
    .RegisterKick    = e_RegisterKick,
    .SetMac          = e_SetMac,
    .SetLinkState    = e_SetLinkState,
    .SetMulticast    = e_SetMulticast,
    .SetPromisc      = e_SetPromisc,
    .GetStats        = e_GetStats,
};

int
e1000e_reference_register(void)
{
    return (int)NetDev_Register("e1000e", &g_e1000e_iface);
}
