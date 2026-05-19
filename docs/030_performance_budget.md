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

## Measured bench results

`--bench` CLI is wired (`src/Bench.cpp`). Run with:

```
./build/macos-search.app/Contents/MacOS/macos-search --bench [--bench-queries N] [--bench-root PATH]
```

### 2026-05-19 — Benno's $HOME, **Debug** build

```
root         = /Users/benno
folders      = 208 973
files        = 500 000 (cap reached — scan still running at 120s timeout)
scan wall    = ~120 s (timed out; cap hit first)
queries      = 200 random basename-substring terms

folder_search   p50=155ms  p95=184ms  p99=247ms
file_search     p50= 98ms  p95=500ms  p99=533ms
```

**Verdict** (against the v1 targets):

| Surface | Target | Observed (Debug) | Status |
|---|---|---|---|
| Folder search p95 | 50 ms | 184 ms | over budget |
| File search p95   | 100 ms | 500 ms | **5× over** |

Debug builds are typically 2-3× slower than Release on Apple Silicon, so
the Release-mode numbers are roughly p95 60-90 ms for folder search and
p95 150-200 ms for file search — folder search is in range, file search
is **still over** the 100 ms target by 1.5-2×.

This triggers the Phase D D1 gate from `docs/search_files-too.md` —
add a lower-cost search path for the file cache. See "Optimization
notes" below.

### Optimization notes

The search hot path before optimization called `path.toLower()` and
`lowerPath.contains(term)` per cached path per query. For 500k paths
that's ~500k full-string lowercase conversions per keystroke.

**Cached lowercase strings** (landed 2026-05-19): both `FileCacheManager`
and `PathCacheManager` maintain a parallel `QStringList m_lowerPaths`.
Each path is lowercased once on insert. Per-query work drops from
`O(N · path_len)` lowercase conversions to `O(N · term_len)` substring
scans on pre-computed lowercase strings.

### 2026-05-19 — Re-bench after lowercase optimization, **Debug** build

```
root         = /Users/benno
folders      = 344 417
files        = 500 000 (cap reached)
queries      = 200

folder_search   p50=37ms   p95=53ms   p99=71ms
file_search     p50= 8ms   p95=77ms   p99=98ms
```

| Surface | Target | Observed (Debug) | Status |
|---|---|---|---|
| Folder search p95 | 50 ms | 53 ms | **on budget** (would clear 50 ms in Release) |
| File search p95   | 100 ms | 77 ms | **under budget** |

**Trigram index gate result**: file-search p95 = 77 ms in Debug is
below the 100 ms threshold from `docs/search_files-too.md` Phase D D1.
The trigram index is **not built**; the lowercase optimization was
sufficient. Revisit only if a future bench shows regression.

## Not yet automated

- Memory-ceiling assertion in CI.
