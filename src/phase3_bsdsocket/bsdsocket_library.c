/*
 * phase3_bsdsocket/bsdsocket_library.c — OS4 .library shell.
 *
 * Wraps libnetstack_client (which does the actual RPC to
 * netstack.process) in a proper AmigaOS 4 .library so existing
 * apps can OpenLibrary("bsdsocket.library", 4) + GetInterface
 * and call socket()/bind()/... via the "main" interface vector
 * table.
 *
 * Library lifecycle:
 *   Init      — runs once at first load. Starts the netstack
 *               engine, initializes the client. Returns the
 *               library base or NULL on failure.
 *   Open      — called on each OpenLibrary. Just bumps OpenCnt.
 *   Close     — called on each CloseLibrary. Decrements OpenCnt.
 *   Expunge   — called when OpenCnt == 0 and exec wants memory
 *               back. Stops the engine, releases seglist.
 *
 * Only the core BSD socket surface is exported today
 * (socket/bind/connect/listen/accept/send/recv/CloseSocket).
 * WaitSelect, getsockopt, gethostbyname, etc. — Phase 3.5.
 */

#include "netstack/netstack.h"
#include "netstack/netstack_client.h"
#include "netstack/netstack_ipc.h"
#include "netstack/osal.h"
#include "netstack/version.h"

#include <dos/dos.h>              /* BPTR */
#include <proto/exec.h>
#include <exec/exectags.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/interfaces.h>
#include <exec/nodes.h>
#include <exec/execbase.h>
#include <string.h>

/* -------- crt0-free globals ------------------------------------
 *
 * Building with -nostartfiles skips newlib's crt0, which
 * normally provides the IExec + INewlib global interface
 * pointers. Provide them here. IExec is set in _bsd_Init from
 * the exec interface passed by MakeLibrary. INewlib stays NULL —
 * we deliberately don't call newlib. Its stub symbol just needs
 * to exist so libc.a doesn't fail to link. */
struct ExecIFace *IExec;
void             *INewlib;

/* -------- Version / identity ---------------------------------- */

/* Use a distinct name during scaffold — don't shadow the
 * system's bsdsocket.library, which real apps (Roadshow, IBrowse)
 * depend on. Renamed to bsdsocket.library once we're confident
 * enough to be a drop-in replacement. */
#define BSDLIBNAME     "netstack.library"
#define BSDLIBVER      4
#define BSDLIBREV      0
#define BSDLIBVERSTR   "netstack.library 4.0 (bsdsocket-compatible scaffold)"

static const char verstag[] __attribute__((used)) =
    "\0$VER: " BSDLIBVERSTR " " __DATE__;

/* -------- Library base ---------------------------------------- */

struct BsdsocketBase {
    struct Library     lib;
    BPTR               seglist;
    struct ExecIFace  *IExec;
};

/* -------- Forward declarations -------------------------------- */

extern struct Library *_bsd_Init(struct Library *lib, BPTR seglist,
                                  struct Interface *exec);

/* Shared Obtain/Release for BOTH interfaces — matches the
 * OS4 SDK Languagedriver example (defaultObtain/defaultRelease).
 * The library manager's Obtain must have the generic
 * struct Interface* signature, not LibraryManagerInterface*,
 * because exec dispatches these via the generic interface
 * calling convention. */
extern uint32           _default_Obtain(struct Interface *);
extern uint32           _default_Release(struct Interface *);
extern struct Library  *_mgr_Open(struct LibraryManagerInterface *, uint32 version);
extern BPTR             _mgr_Close(struct LibraryManagerInterface *);
extern BPTR             _mgr_Expunge(struct LibraryManagerInterface *);

/* "main" interface: user-facing API. */
extern LONG bs_socket(struct Interface *, LONG domain, LONG type, LONG proto);
extern LONG bs_bind(struct Interface *, LONG s, const void *addr, LONG addrlen);
extern LONG bs_connect(struct Interface *, LONG s, const void *addr, LONG addrlen);
extern LONG bs_listen(struct Interface *, LONG s, LONG backlog);
extern LONG bs_accept(struct Interface *, LONG s, void *addr, LONG *addrlen);
extern LONG bs_send(struct Interface *, LONG s, const void *buf, LONG len, LONG flags);
extern LONG bs_recv(struct Interface *, LONG s, void *buf, LONG len, LONG flags);
extern LONG bs_CloseSocket(struct Interface *, LONG s);

/* -------- Vector tables --------------------------------------- */

/* Manager interface: exec-mandated first 4 slots
 * (Obtain, Release, Expunge-of-Interface, Clone), then the
 * library-mgr slots (Open, Close, LibExpunge, GetInterface). */
static const APTR _mgr_vectors[] = {
    (APTR)_default_Obtain,
    (APTR)_default_Release,
    (APTR)NULL,       /* Expunge (Interface) — unused */
    (APTR)NULL,       /* Clone — unused */
    (APTR)_mgr_Open,
    (APTR)_mgr_Close,
    (APTR)_mgr_Expunge,
    (APTR)NULL,       /* GetInterface — NULL uses exec's default */
    (APTR)-1,
};

static const struct TagItem _mgr_tags[] = {
    { MIT_Name,        (ULONG)"__library" },
    { MIT_VectorTable, (ULONG)_mgr_vectors },
    { MIT_Version,     1 },
    { TAG_END, 0 },
};

/* Shared default Obtain/Release used by both interfaces —
 * mirrors the OS4 SDK Languagedriver example (defaultObtain /
 * defaultRelease). Same fn is legal to use across multiple
 * interfaces because it operates on Self->Data.RefCount which
 * is per-interface. */
uint32 APICALL
_default_Obtain(struct Interface *Self)
{
    Self->Data.RefCount++;
    return Self->Data.RefCount;
}

uint32 APICALL
_default_Release(struct Interface *Self)
{
    Self->Data.RefCount--;
    return Self->Data.RefCount;
}

/* "main" interface: user API. Exec-mandated first 4 slots, then
 * the socket functions. */
static const APTR _main_vectors[] = {
    (APTR)_default_Obtain,
    (APTR)_default_Release,
    (APTR)NULL,           /* Expunge — unused */
    (APTR)NULL,           /* Clone   — unused */
    (APTR)bs_socket,
    (APTR)bs_bind,
    (APTR)bs_connect,
    (APTR)bs_listen,
    (APTR)bs_accept,
    (APTR)bs_send,
    (APTR)bs_recv,
    (APTR)bs_CloseSocket,
    (APTR)-1,
};

static const struct TagItem _main_tags[] = {
    { MIT_Name,        (ULONG)"main" },
    { MIT_VectorTable, (ULONG)_main_vectors },
    { MIT_DataSize,    0 },   /* no per-interface private data */
    { MIT_Version,     1 },
    { TAG_END, 0 },
};

/* Array of tag lists, NULL-terminated. Library manager MUST be
 * first per OS4 convention.
 *
 * KNOWN LIMITATION (still): even with SDK-example-matched
 * patterns (unified default_Obtain/Release taking
 * struct Interface*, explicit lib_Node fields in Init,
 * MIT_DataSize=0 on main tags), enabling _main_tags here
 * still hangs the guest at OpenLibrary. Something else exec
 * validates on the second interface is off. Left commented out;
 * see git log for the debug trail. Callers should link
 * libnetstack_client.a directly until this is unblocked. */
static const APTR bsd_interfaces[] = {
    (APTR)_mgr_tags,
    /* (APTR)_main_tags, */
    (APTR)NULL,
};

/* -------- CLT_ init tags + Resident ---------------------------- */

static struct TagItem bsd_init_tags[] = {
    { CLT_DataSize,   sizeof(struct BsdsocketBase) },
    { CLT_Interfaces, (ULONG)bsd_interfaces },
    { CLT_InitFunc,   (ULONG)_bsd_Init },
    { TAG_END, 0 },
};

/* If someone runs this file as an executable, print a helpful
 * message and exit. Real .library entry is via Resident tag. */
int
_start(char *argstring, int arglen, struct ExecBase *sysbase)
{
    (void)argstring; (void)arglen;
    struct ExecIFace *IE = (struct ExecIFace *)sysbase->MainInterface;
    IE->DebugPrintF("%s is a library — install to LIBS: and OpenLibrary()\n",
                    BSDLIBNAME);
    return 20;
}

static struct Resident bsd_res __attribute__((used)) = {
    RTC_MATCHWORD,
    (struct Resident *)&bsd_res,
    (struct Resident *)(&bsd_res + 1),
    RTF_NATIVE | RTF_AUTOINIT,
    BSDLIBVER,
    NT_LIBRARY,
    0,
    (STRPTR)BSDLIBNAME,
    (STRPTR)BSDLIBVERSTR,
    (APTR)bsd_init_tags,
};

/* -------- Library Init ---------------------------------------- */

struct Library *
_bsd_Init(struct Library *lib, BPTR seglist, struct Interface *exec)
{
    struct BsdsocketBase *base = (struct BsdsocketBase *)lib;
    struct ExecIFace *IExec_local = (struct ExecIFace *)exec;

    base->seglist = seglist;
    base->IExec   = IExec_local;
    IExec         = IExec_local;   /* publish global for the OSAL */

    /* Mirror the OS4 SDK Languagedriver example — set the standard
     * library node fields. MakeLibrary sets some of these but not
     * all; being explicit is the SDK-recommended pattern. */
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Pri  = 0;
    base->lib.lib_Node.ln_Name = (STRPTR)BSDLIBNAME;
    base->lib.lib_Flags        = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version      = BSDLIBVER;
    base->lib.lib_Revision     = BSDLIBREV;
    base->lib.lib_IdString     = (STRPTR)BSDLIBVERSTR;

    /* Do NOT start the engine here — Init runs under Forbid()
     * and a synchronous Wait would deadlock. Engine spins up
     * lazily on the first user API call (ensure_engine below). */

    return (struct Library *)base;
}

/* Lazy engine start — called from every bs_* function before
 * making the RPC. Idempotent + cheap after first success. */
static int g_engine_started;

static int
ensure_engine(void)
{
    if (g_engine_started) return 0;
    if (NetstackEngine_Start(NULL) != 0) return -1;
    if (netstack_client_init() != 0)     return -1;
    g_engine_started = 1;
    return 0;
}

/* -------- Library manager --------------------------------------

Obtain / Release refcount the INTERFACE (used by GetInterface /
DropInterface). Open / Close refcount the LIBRARY (used by
OpenLibrary / CloseLibrary). Expunge frees the library base
+ seglist when refcount is 0. */

uint32
_mgr_Obtain(struct LibraryManagerInterface *Self)
{
    Self->Data.RefCount++;
    return Self->Data.RefCount;
}

uint32
_mgr_Release(struct LibraryManagerInterface *Self)
{
    Self->Data.RefCount--;
    return Self->Data.RefCount;
}

struct Library *
_mgr_Open(struct LibraryManagerInterface *Self, uint32 version)
{
    (void)version;
    struct BsdsocketBase *base = (struct BsdsocketBase *)Self->Data.LibBase;
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return (struct Library *)base;
}

BPTR
_mgr_Close(struct LibraryManagerInterface *Self)
{
    struct BsdsocketBase *base = (struct BsdsocketBase *)Self->Data.LibBase;
    base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP)) {
        return _mgr_Expunge(Self);
    }
    return (BPTR)NULL;
}

BPTR
_mgr_Expunge(struct LibraryManagerInterface *Self)
{
    struct BsdsocketBase *base = (struct BsdsocketBase *)Self->Data.LibBase;
    struct ExecIFace *IExec_local = base->IExec;

    if (base->lib.lib_OpenCnt > 0) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return (BPTR)NULL;
    }

    BPTR sl = base->seglist;

    /* Tear down engine + client. */
    netstack_client_shutdown();
    NetstackEngine_Stop();

    /* Remove ourselves from the library list. */
    IExec_local->Remove((struct Node *)&base->lib.lib_Node);
    IExec_local->DeleteLibrary(&base->lib);
    return sl;
}

/* -------- The "main" interface — user API --------------------- */

/*
 * bsdsocket uses classic AmigaOS calling convention: LONG return
 * with a small errno set out-of-band. Our client wrappers return
 * -errno directly. Translate: on <0 return, we return -1 (POSIX
 * "error indicator") but do NOT yet publish a per-task errno
 * (Phase 3.5 detail).
 *
 * Address marshalling: only sockaddr_in for now. Extract sin_port
 * (2 bytes at offset 2, network byte order = big-endian, so on PPC
 * BE it's a straight 16-bit load).
 */

static uint16
extract_port(const void *addr, LONG addrlen)
{
    if (!addr || addrlen < 4) return 0;
    const uint8 *p = (const uint8 *)addr;
    /* sockaddr_in layout: [0]=sin_len,[1]=sin_family,[2..3]=sin_port_be */
    return ((uint16)p[2] << 8) | (uint16)p[3];
}

LONG
bs_socket(struct Interface *Self, LONG domain, LONG type, LONG proto)
{
    (void)Self;
    if (ensure_engine() != 0) return -1;
    int sock = -1;
    int rv = netstack_socket((int)domain, (int)type, (int)proto, &sock);
    if (rv != NETSTACK_OK) return -1;
    return (LONG)sock;
}

LONG
bs_bind(struct Interface *Self, LONG s, const void *addr, LONG addrlen)
{
    (void)Self;
    if (ensure_engine() != 0) return -1;
    uint16 port = extract_port(addr, addrlen);
    int rv = netstack_bind((int)s, port);
    return rv == NETSTACK_OK ? 0 : -1;
}

LONG
bs_connect(struct Interface *Self, LONG s, const void *addr, LONG addrlen)
{
    (void)Self;
    if (ensure_engine() != 0) return -1;
    uint16 port = extract_port(addr, addrlen);
    int rv = netstack_connect((int)s, port);
    return rv == NETSTACK_OK ? 0 : -1;
}

LONG
bs_listen(struct Interface *Self, LONG s, LONG backlog)
{
    (void)Self;
    if (ensure_engine() != 0) return -1;
    int rv = netstack_listen((int)s, (int)backlog);
    return rv == NETSTACK_OK ? 0 : -1;
}

LONG
bs_accept(struct Interface *Self, LONG s, void *addr, LONG *addrlen)
{
    (void)Self;
    if (ensure_engine() != 0) return -1;
    int new_sock = -1;
    uint16 peer_port = 0;
    int rv = netstack_accept((int)s, &new_sock, &peer_port);
    if (rv != NETSTACK_OK) return -1;
    if (addr && addrlen && *addrlen >= 8) {
        uint8 *p = (uint8 *)addr;
        p[0] = 16;
        p[1] = 2;
        p[2] = (uint8)(peer_port >> 8);
        p[3] = (uint8)(peer_port & 0xFF);
        for (int i = 4; i < 16 && i < *addrlen; i++) p[i] = 0;
        *addrlen = 16;
    }
    return (LONG)new_sock;
}

LONG
bs_send(struct Interface *Self, LONG s, const void *buf, LONG len, LONG flags)
{
    (void)Self; (void)flags;
    if (ensure_engine() != 0) return -1;
    int rv = netstack_send((int)s, buf, (int)len);
    return (rv >= 0) ? (LONG)rv : -1;
}

LONG
bs_recv(struct Interface *Self, LONG s, void *buf, LONG len, LONG flags)
{
    (void)Self; (void)flags;
    if (ensure_engine() != 0) return -1;
    int rv = netstack_recv((int)s, buf, (int)len);
    return (rv >= 0) ? (LONG)rv : -1;
}

LONG
bs_CloseSocket(struct Interface *Self, LONG s)
{
    (void)Self;
    if (ensure_engine() != 0) return -1;
    int rv = netstack_close((int)s);
    return rv == NETSTACK_OK ? 0 : -1;
}
