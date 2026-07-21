#ifndef BENCH_H
#define BENCH_H

#include <QString>

// `--bench` CLI handler. Runs a one-shot benchmark: warms the cache against
// $HOME (or the path passed via --bench-root=PATH), runs N filename queries
// against both folder and file caches, computes p50/p95/p99 latency, samples
// process memory (mach task_info phys_footprint/resident_size) at baseline /
// after-scan / after-queries, and emits JSON to stdout.
//
// Returns the process exit code (0 on success). Use as:
//
//     int rc = Bench::runIfRequested(app, argc, argv);
//     if (rc >= 0) return rc;
//
// When `--bench` is not present, returns -1 and `main()` proceeds normally.
namespace Bench {

int runIfRequested(int argc, char *argv[]);

}  // namespace Bench

#endif // BENCH_H
