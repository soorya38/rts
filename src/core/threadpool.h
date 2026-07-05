/*=============================================================
 * threadpool.h  –  Persistent worker pool for data-parallel
 *                  simulation passes
 *
 * tp_parallel_for() splits [0, count) into chunks and runs `fn`
 * on the worker threads plus the calling thread, blocking until
 * every chunk has completed.  The pool is created lazily on
 * first use and sized to the machine's core count.
 *
 * Rules for jobs:
 *   - fn(start, end, ctx) must only write to per-index output
 *     slots (or otherwise disjoint memory) so results are
 *     independent of thread scheduling — the simulation must
 *     stay deterministic for lockstep multiplayer.
 *   - Jobs must not call tp_parallel_for recursively; nested
 *     calls run serially on the calling thread.
 *=============================================================*/
#ifndef RTS_THREADPOOL_H
#define RTS_THREADPOOL_H

typedef void (*tp_range_fn)(int start, int end, void *ctx);

/* Minimum live-unit count before the O(n²) simulation scans are
   worth dispatching to the pool; below this they run inline. */
#define TP_UNIT_SCAN_THRESHOLD 64

/* Run fn over [0, count) in parallel; blocks until complete. */
void tp_parallel_for(int count, tp_range_fn fn, void *ctx);

/* Number of pool worker threads (0 = running serially). */
int tp_worker_count(void);

#endif /* RTS_THREADPOOL_H */
