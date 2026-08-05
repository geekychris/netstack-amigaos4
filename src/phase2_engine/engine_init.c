/*
 * phase2_engine/engine_init.c — netstack.process subsystem init.
 *
 * STATUS: stubs. Real impl calls into rump kernel to bring up
 * ARP tables, routing, socket subsystem, loopback interface, etc.
 */

#include "netstack/netstack.h"
#include "netstack/osal.h"

/* Called from engine_process_main after rump_init() returns. */
int
netstack_init_ifaces(void)
{
    /* TODO:
     *   1. rump_pub_ifconfig_create("lo0") — or the newer API.
     *   2. rump_pub_ifconfig_ipv4addr("lo0", "127.0.0.1", "255.0.0.0").
     *   3. rump_pub_ifconfig_up("lo0").
     * Reference: NetBSD's librumpnet_config.
     */
    return 0;
}

int
netstack_init_sysctl(void)
{
    /* TODO: bind sysctl surface to a caller-accessible RPC or
     * a small AmigaOS-side utility (netstack ctl get/set ...). */
    return 0;
}
