/*
 * tests/phase1/test_cv.c — condition-variable smoke test.
 *
 * Bounded producer/consumer queue guarded by a mutex; empty→wait
 * on cv_notempty, full→wait on cv_notfull. N producers push
 * ITEMS items each; M consumers pop until they've drained
 * N*ITEMS. Verifies both cv_wait and cv_signal fire correctly.
 *
 * Success: sum of popped items == expected sum; no deadlock;
 * every producer + consumer thread joins cleanly.
 */

#include "netstack/osal.h"
#include <proto/exec.h>
#include <proto/dos.h>

#define N_PROD   3
#define N_CONS   2
#define ITEMS    50           /* per producer */
#define Q_SIZE   4

static struct osal_mutex *g_lock;
static struct osal_cv    *g_notempty;
static struct osal_cv    *g_notfull;

static int  g_q[Q_SIZE];
static int  g_qhead;
static int  g_qtail;
static int  g_qcount;

static volatile int g_produced;   /* total items pushed */
static volatile int g_consumed;   /* total items popped */
static volatile int g_sum_pushed;
static volatile int g_sum_popped;

static void
producer(void *arg)
{
    int id = (int)(ULONG)(APTR)arg;
    for (int n = 0; n < ITEMS; n++) {
        int item = id * 10000 + n;
        osal_mutex_lock(g_lock);
        while (g_qcount == Q_SIZE) osal_cv_wait(g_notfull, g_lock);
        g_q[g_qhead] = item;
        g_qhead = (g_qhead + 1) % Q_SIZE;
        g_qcount++;
        g_produced++;
        g_sum_pushed += item;
        osal_cv_signal(g_notempty);
        osal_mutex_unlock(g_lock);
    }
}

static void
consumer(void *arg)
{
    (void)arg;
    const int target = N_PROD * ITEMS;
    for (;;) {
        osal_mutex_lock(g_lock);
        while (g_qcount == 0 && g_consumed < target) {
            osal_cv_wait(g_notempty, g_lock);
        }
        if (g_qcount == 0 && g_consumed >= target) {
            /* Nothing left and never will be — exit. */
            osal_mutex_unlock(g_lock);
            break;
        }
        int item = g_q[g_qtail];
        g_qtail = (g_qtail + 1) % Q_SIZE;
        g_qcount--;
        g_consumed++;
        g_sum_popped += item;
        osal_cv_signal(g_notfull);
        osal_mutex_unlock(g_lock);
    }
}

int
main(int argc, char **argv)
{
    (void)argc; (void)argv;
    IDOS->Printf("test_cv: %ld producers x %ld items, %ld consumers, queue %ld\n",
                 (LONG)N_PROD, (LONG)ITEMS, (LONG)N_CONS, (LONG)Q_SIZE);
    IDOS->FFlush(IDOS->Output());

    g_lock     = osal_mutex_new();
    g_notempty = osal_cv_new();
    g_notfull  = osal_cv_new();
    if (!g_lock || !g_notempty || !g_notfull) {
        IDOS->Printf("primitive alloc failed\n"); return 20;
    }

    struct osal_thread *prods[N_PROD], *cons[N_CONS];
    for (int i = 0; i < N_PROD; i++) {
        osal_thread_create(producer, (void *)(APTR)(ULONG)(i + 1),
                           "cv-prod", 0, &prods[i]);
    }
    for (int i = 0; i < N_CONS; i++) {
        osal_thread_create(consumer, NULL, "cv-cons", 0, &cons[i]);
    }

    /* Wait for producers to finish. */
    for (int i = 0; i < N_PROD; i++) osal_thread_join(prods[i]);

    /* Broadcast to wake any consumer stuck on empty-queue-and-
     * we're-done — otherwise they'd never notice production ended. */
    osal_mutex_lock(g_lock);
    osal_cv_broadcast(g_notempty);
    osal_mutex_unlock(g_lock);

    for (int i = 0; i < N_CONS; i++) osal_thread_join(cons[i]);

    LONG expected_produced = N_PROD * ITEMS;
    LONG ok = (g_produced == expected_produced &&
               g_consumed == expected_produced &&
               g_sum_popped == g_sum_pushed);

    IDOS->Printf("produced=%ld  consumed=%ld  (expected %ld)\n",
                 (LONG)g_produced, (LONG)g_consumed, expected_produced);
    IDOS->Printf("sum pushed=%ld  sum popped=%ld  (match: %s)\n",
                 (LONG)g_sum_pushed, (LONG)g_sum_popped,
                 g_sum_pushed == g_sum_popped ? "YES" : "NO");
    IDOS->Printf("test_cv: %s\n", ok ? "PASS" : "FAIL");
    IDOS->FFlush(IDOS->Output());

    osal_cv_free(g_notempty);
    osal_cv_free(g_notfull);
    osal_mutex_free(g_lock);
    return ok ? 0 : 20;
}
