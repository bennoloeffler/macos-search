# Performance Budget

What's empirically observed today + the targets the implementation is
designed for. Sources of truth: the Swift spec at
`../maude_manual_search/docs/CACHED-SEARCH-SPEC.md` and the upstream
behavior of `PathCacheManager`.

## Observed today

From a Debug build on Apple Silicon (M-series Mac, macOS 14.8.5),
single run on Benno's `$HOME`:

| Phase | Observed |
|---|---|
| `main()` → window visible | ≲ 200 ms (cold) |
| Window visible → first usable keystroke | well under one frame (no observable lag) |
| Scan throughput | ~26 600 folders in the first second; ~73 000 by t≈3 s |
| Final scan total | rises through 70 k–80 k folders on Benno's box (depending on which subtrees are walked) |
| Exclusion ratio | ~1 % of entries (`node_modules`, `.git`, etc.) |

These are **Debug** numbers. Release should be noticeably faster — no
optimizer-disabling flags are set in `CMakeLists.txt`.

## Latency targets

| Phase | Budget | How to measure |
|---|---|---|
| App launch → window visible | ≤ **500 ms** cold, ≤ **150 ms** warm | wallclock from `main()` to `show()` |
| Steady-state in-memory search (`PathCacheManager::search`) | ≤ **50 ms** for 200 k cached paths | benchmark in tests (not yet automated) |
| Streaming result paint during partial-cache state | ~**100 ms** cadence | `cacheUpdated` signal frequency from `PathCacheManager` |
| FSEvents → cache → next search reflects change | ≤ **200 ms** | manual: create temp dir under `$HOME`, search |

## Throughput targets

- Initial BFS of a typical `$HOME` (50 k–300 k entries) finishes
  in **< 30 s** on Apple Silicon, **< 90 s** on Intel.
- The cache thread is `QThreadPool` parallel — see `PathCacheManager::scanWorker`.

## Memory

- For 200 k paths, expected ceiling **≤ 100 MB** RSS. Not yet measured
  on this repo's build. Two storage structures: `QStringList m_paths`
  and `QSet<QString> m_pathSet` (O(1) lookup).

## CPU

- Idle, post-scan: < 1 % CPU.
- During scan: one core per worker thread until BFS queue drains.

## Failure modes that must NOT happen

These all hold in the current code, per upstream:

- UI never blocks on the scan — `QFileSystemWatcher`, `QtConcurrent`, and
  worker threads keep the main thread free.
- Symlink loops don't recurse forever — BFS dedupes via `m_pathSet`.
- Permission-denied subtrees are skipped silently (counted in
  `foldersExcluded`, no error dialog).
- `QFileSystemWatcher`'s ~8 k path limit is handled via FSEvents on macOS
  (Qt's watcher uses FSEvents under the hood on macOS, not kqueue, so the
  limit doesn't bite the way it does on Linux's inotify).

## Not yet automated

- A `--bench` CLI flag that runs the scan, prints JSON timings, exits.
  Useful for regression-testing perf. Tracked in `090_open_questions.md`.
- Memory-ceiling assertion in CI.
