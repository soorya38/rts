/*=============================================================
 * threadpool.c  –  Persistent worker pool for per-frame jobs
 *
 * Built for many small dispatches per frame (60 Hz sim), so the
 * hot path is lock-free: workers claim chunks from an atomic
 * cursor via CAS, spin briefly waiting for the next job, and
 * only park on a condvar after the spin budget runs out.  The
 * calling thread participates in every job, so a pool of N
 * workers gives N+1 lanes of execution.
 *
 * The chunk cursor packs (generation << 32 | next_index) into a
 * single 64-bit atomic: a worker that wakes up late and races a
 * newer job sees a mismatched generation and backs off instead
 * of claiming (or corrupting) the new job's range.
 *
 * Used by the simulation for data-parallel passes (fog of war,
 * unit/tower target acquisition).  See threadpool.h for the
 * determinism rules jobs must follow.
 *=============================================================*/
#include "threadpool.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#define TP_MAX_WORKERS 8

/* Spin iterations before a worker parks on the condvar.  Covers
   the gaps between same-frame dispatches; workers still sleep
   during rendering / between frames. */
#define TP_SPIN_BUDGET 20000

static pthread_t       tp_threads[TP_MAX_WORKERS];
static int             tp_nworkers = 0;
static pthread_mutex_t tp_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  tp_work_cv = PTHREAD_COND_INITIALIZER;
static int             tp_parked  = 0;   /* workers asleep (guarded by tp_mutex) */

/* Job description: written by the publisher before the cursor's
   generation bump (release store) and read by workers after an
   acquire load of the cursor, so plain fields are safe. */
static tp_range_fn tp_job_fn;
static void       *tp_job_ctx;
static int         tp_job_count;
static int         tp_job_chunk;

static _Atomic uint64_t tp_cursor = 0;  /* generation<<32 | next index */
static atomic_int       tp_done   = 0;  /* items completed this job    */

/* Prevents nested tp_parallel_for calls from deadlocking: they
   just run serially on the calling thread. */
static _Thread_local bool tp_in_job = false;

static inline void tp_cpu_relax(void)
{
#if defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield");
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ __volatile__("pause");
#endif
}

/* Claim and run chunks of the job identified by `gen` until the
   range is exhausted or a newer job replaces it. */
static void tp_run_chunks(uint64_t gen)
{
    tp_range_fn fn    = tp_job_fn;
    void       *ctx   = tp_job_ctx;
    int         count = tp_job_count;
    int         chunk = tp_job_chunk;

    tp_in_job = true;
    uint64_t cur = atomic_load_explicit(&tp_cursor, memory_order_acquire);
    for (;;) {
        if ((cur >> 32) != gen) break;          /* a newer job took over */
        int start = (int)(cur & 0xffffffffu);
        if (start >= count) break;              /* range exhausted */
        int end = start + chunk;
        if (end > count) end = count;

        uint64_t next = (gen << 32) | (uint32_t)end;
        if (atomic_compare_exchange_weak_explicit(&tp_cursor, &cur, next,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
            fn(start, end, ctx);
            atomic_fetch_add_explicit(&tp_done, end - start,
                                      memory_order_release);
            cur = atomic_load_explicit(&tp_cursor, memory_order_acquire);
        }
        /* CAS failure reloaded `cur`; just retry. */
    }
    tp_in_job = false;
}

static void *tp_worker_main(void *arg)
{
    (void)arg;
    uint64_t seen_gen = 0;
    int spins = 0;

    for (;;) {
        uint64_t cur = atomic_load_explicit(&tp_cursor, memory_order_acquire);
        uint64_t gen = cur >> 32;
        if (gen != seen_gen) {
            seen_gen = gen;
            spins = 0;
            tp_run_chunks(gen);
            continue;
        }

        if (++spins < TP_SPIN_BUDGET) {
            tp_cpu_relax();
            continue;
        }

        /* No work for a while — park until the next job. */
        pthread_mutex_lock(&tp_mutex);
        tp_parked++;
        while ((atomic_load_explicit(&tp_cursor, memory_order_acquire) >> 32)
               == seen_gen) {
            pthread_cond_wait(&tp_work_cv, &tp_mutex);
        }
        tp_parked--;
        pthread_mutex_unlock(&tp_mutex);
        spins = 0;
    }
    /* Workers live for the whole process; no shutdown path needed. */
    return NULL;
}

static pthread_once_t tp_once = PTHREAD_ONCE_INIT;

static void tp_init(void)
{
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    int wanted = (int)cores - 1;  /* the calling thread is a lane too */
    if (wanted > TP_MAX_WORKERS) wanted = TP_MAX_WORKERS;

    /* RTS_THREADS caps total lanes (workers + caller);
       RTS_THREADS=1 forces fully serial execution. */
    const char *env = getenv("RTS_THREADS");
    if (env) {
        int lanes = atoi(env);
        if (lanes >= 1 && lanes - 1 < wanted) wanted = lanes - 1;
    }

    for (int i = 0; i < wanted; i++) {
        if (pthread_create(&tp_threads[i], NULL, tp_worker_main, NULL) != 0) {
            break;  /* keep however many workers we managed to start */
        }
        tp_nworkers = i + 1;
    }
}

int tp_worker_count(void)
{
    pthread_once(&tp_once, tp_init);
    return tp_nworkers;
}

void tp_parallel_for(int count, tp_range_fn fn, void *ctx)
{
    if (count <= 0) return;

    pthread_once(&tp_once, tp_init);

    if (tp_nworkers == 0 || tp_in_job || count < 4) {
        fn(0, count, ctx);
        return;
    }

    /* Publish the job.  Plain stores first, then the release
       store of the bumped generation makes them visible. */
    uint64_t gen = (atomic_load_explicit(&tp_cursor, memory_order_relaxed)
                    >> 32) + 1;
    tp_job_fn    = fn;
    tp_job_ctx   = ctx;
    tp_job_count = count;
    /* ~4 chunks per lane so faster threads can steal extra work. */
    tp_job_chunk = count / ((tp_nworkers + 1) * 4) + 1;
    atomic_store_explicit(&tp_done, 0, memory_order_relaxed);
    atomic_store_explicit(&tp_cursor, gen << 32, memory_order_release);

    /* Wake parked workers; spinning ones notice the cursor alone. */
    pthread_mutex_lock(&tp_mutex);
    if (tp_parked > 0) pthread_cond_broadcast(&tp_work_cv);
    pthread_mutex_unlock(&tp_mutex);

    /* Participate, then wait for stragglers (at most one chunk's
       worth of work remains, so the spin tail is short). */
    tp_run_chunks(gen);
    while (atomic_load_explicit(&tp_done, memory_order_acquire) < count) {
        tp_cpu_relax();
    }
}
