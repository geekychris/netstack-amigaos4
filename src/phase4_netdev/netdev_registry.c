/*
 * phase4_netdev/netdev_registry.c — NetDev driver registration.
 *
 * STATUS: skeleton. Fixed-size table of registered drivers; no
 * dynamic growth, no locking (single-threaded init assumption
 * for now). Real impl needs a semaphore + growable table.
 */

#include "netstack/netdev.h"
#include "netstack/osal.h"

#include <proto/exec.h>
#include <string.h>

#define NETDEV_MAX_REGISTERED 16

struct netdev_slot {
    const char        *name;
    struct NetDevIF   *iface;
};

static struct netdev_slot g_registry[NETDEV_MAX_REGISTERED];
static int                g_count;

LONG
NetDev_Register(const char *name, struct NetDevIF *iface)
{
    if (!name || !iface) return -1;
    if (g_count >= NETDEV_MAX_REGISTERED) return -1;
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_registry[i].name, name) == 0) return -1;
    }
    g_registry[g_count].name  = name;
    g_registry[g_count].iface = iface;
    g_count++;
    return 0;
}

LONG
NetDev_Unregister(const char *name)
{
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_registry[i].name, name) == 0) {
            g_registry[i] = g_registry[g_count - 1];
            g_count--;
            return 0;
        }
    }
    return -1;
}

struct NetDevIF *
NetDev_Lookup(const char *name)
{
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_registry[i].name, name) == 0)
            return g_registry[i].iface;
    }
    return 0;
}

LONG
NetDev_List(char *out_names[], LONG max_names)
{
    LONG n = g_count < max_names ? g_count : max_names;
    for (LONG i = 0; i < n; i++) out_names[i] = (char *)g_registry[i].name;
    return n;
}
