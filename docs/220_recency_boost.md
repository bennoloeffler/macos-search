# Recency Boost — surfacing newly-created / recently-changed paths first

**Status:** design / concept only. Nothing here is built. This is the
architecture an implementer should follow without re-deciding.

> **⚠️ Architecture-reference correction (2026-07-21).** This doc was
> drafted in an isolated worktree that had forked from the session
> baseline `5c009ad`, *before* PathStore, the persistent index, and the
> FSEvents watcher landed. Its "Reality check" section therefore claims
> `src/PathStore.h`, `docs/200`/`docs/210`, and `src/FsEventsWatcher.h`
> "do not exist" — **on current `main` they all do.** The *design* is
> unaffected and transfers directly; only remap two references when
> implementing:
>
> - **Live change signal** is now `FsEventsWatcher::directoryChanged`
>   (real recursive FSEvents), still delivered to
>   `PathCacheManager::onDirectoryChanged` on the **main thread** — the
>   lock-free argument below still holds.
> - **Key the touched-set on PathStore node-ids** (via `m_store->find`)
>   rather than raw path strings; the bounded-ring, age-bucket, and
>   additive-sort-bonus design is otherwise unchanged. The doc's own
>   forward-compat note already anticipated this.
>
> Everything else (bounded ring sizing, memory math, ranking formula,
> adversarial review, measured gates) applies as written.

## Goal

When the user has *just created or modified* a file or folder, searching
for it should surface it **at the top** of the results, above older,
equally-good name matches. Concretely, the user types the name of the
thing they made two minutes ago and it is the first hit — not buried
under twenty year-old folders with the same substring.

This must hold **without**:

- surfacing a path that is *not* a name match (recency never invents
  relevance — see [Ranking](#ranking-integration));
- reshuffling the result list every second (see [Age bucketing](#age-bucketing));
- adding measurable search latency or unbounded memory (see [Gates](#measured-gates)).

---

## Reality check — the storage this fork actually has

The task brief references `src/PathStore.h` (a 12-byte packed `Node`),
`docs/200_pathstore_redesign.md`, `docs/210_persistent_index.md`
(generation mark/sweep pad bits), and `src/FsEventsWatcher.h`. **None of
these exist in this tree.** The current, shipping storage is:

- `PathCacheManager`: `QStringList m_paths`, a parallel
  `QStringList m_lowerPaths` (pre-lowercased, added 2026-05-19), and
  `QSet<QString> m_pathSet` for O(1) membership. Guarded by one
  `QMutex m_mutex`. (`src/PathCacheManager.h:99-105`.)
- `FileCacheManager`: the same shape for files.
- Live change signal: **`QFileSystemWatcher::directoryChanged`** (Qt
  wraps FSEvents under the hood on macOS), delivered to
  `PathCacheManager::onDirectoryChanged(const QString&)` — a slot on the
  singleton, which lives on the **main thread**
  (`src/PathCacheManager.cpp:30, 564`).
- Search: `FolderSearchWorker` / `FileSearchWorker` are `new …(this)`
  where `this` is the dialog — i.e. they also live on the **main
  thread**. `performSearch()` reads the cache under `m_mutex` and sorts
  ≤100 results by `score desc, path length asc`
  (`src/FolderSearchWorker.cpp:40-86`, `src/FileSearchWorker.cpp:45-82`).

Two consequences drive the whole design:

1. **The natural per-entry timestamp slot does not exist.** There is no
   spare pad bit to claim (there is no packed `Node` at all). A per-entry
   mtime would mean growing the `QStringList`-parallel arrays, and — for
   cold-start ordering — a `stat()` per entry during the scan of
   200k–500k paths. That is a real throughput hit on the hot scan path.
2. **The live change path and the search path are the same thread.** A
   recency structure touched only by `onDirectoryChanged` (write) and
   `performSearch` (read) needs **no lock**. This is the single most
   important simplification available and v1 is built around it.

> Forward-compat note: if the `PathStore` redesign (docs 200/210) ever
> lands, the recency-bucket lookup (`recencyBucket(path)`) is the only
> integration seam and can be reimplemented against node-ids. Do **not**
> couple recency to the (future, not-yet-existing) 15-gen + 1-listed pad
> bits — keep it in its own side structure as described below.

---

## Recommended design (v1): a bounded, live "recently-touched" set

**Recommendation: Option (ii) — a bounded recently-touched structure fed
by the FSEvents/`onDirectoryChanged` main-thread path. Live changes
only. No per-entry mtime, no cold-start recency in v1.**

Rationale:

- The user's stated need is recency **of change** ("I just made this") —
  which is *exactly* the live FSEvents signal, the strongest and most
  recent source. A per-entry mtime (Option i) mostly buys cold-start
  ordering, which the user did not ask for and which costs a `stat()`
  per scanned entry.
- The bounded structure costs **near-zero memory** (proof below) and,
  because it is main-thread-only, **zero locks**.
- It cannot regress search latency: the boost is an O(1) hash lookup per
  candidate, applied only to the ≤100 already-materialized results.

### The structure — `RecencyTracker`

A small main-thread-only object owned by `PathCacheManager` (or a tiny
singleton; owner is irrelevant as long as writes/reads stay main-thread).

```cpp
// RecencyTracker.h  — NOT thread-safe by design; main thread only.
// Keyed by absolute path (covers BOTH folders and files, because
// onDirectoryChanged marks folder-adds and file-adds into the same set).
class RecencyTracker {
public:
    // Called from PathCacheManager::onDirectoryChanged (main thread) when a
    // new dir or file is detected, or an existing one is seen to change.
    void markTouched(const QString &path);      // records now() against path

    // Called from performSearch (main thread). Returns a coarse age bucket:
    //   3 = touched < 1h, 2 = < 1d, 1 = < 7d, 0 = older/unknown.
    int recencyBucket(const QString &path) const;

private:
    struct Slot { QString path; qint64 touchedMsecs; };
    static constexpr int kCapacity = 8192;       // hard bound (see memory math)
    QHash<QString, qint64> m_touchedAt;          // path -> epoch ms  (O(1) lookup)
    QQueue<QString> m_order;                      // FIFO for eviction (ring)
};
```

`markTouched`:

```
now = QDateTime::currentMSecsSinceEpoch();
if (m_touchedAt.contains(path)) { m_touchedAt[path] = now; return; }   // refresh, no growth
if (m_order.size() >= kCapacity) { m_touchedAt.remove(m_order.dequeue()); }  // evict oldest
m_touchedAt.insert(path, now);
m_order.enqueue(path);
```

`recencyBucket` derives the bucket **at query time** from the stored
timestamp (not stored as a bucket — a bucket would silently age wrong):

```
it = m_touchedAt.find(path); if (it == end) return 0;
ageMs = now - *it;
if (ageMs < 1h)  return 3;
if (ageMs < 1d)  return 2;
if (ageMs < 7d)  return 1;
return 0;
```

### Injection point — where `markTouched` is called

Inside `PathCacheManager::onDirectoryChanged` (main thread), at the exact
points that already detect *new* entries, **after** the exclude filter so
excluded dirs never enter the set:

- the "Find new directories" loop (`src/PathCacheManager.cpp:626-641`):
  call `markTouched(fullPath)` right where `addPathToCache(fullPath)` is
  called — but note this is the *directory-diff* branch, which only runs
  on the main thread, so it is safe.
- the file-level "Added files" branch (`src/PathCacheManager.cpp:672+`):
  `markTouched(newFilePath)` for each newly-seen file.

**Do NOT call `markTouched` inside `addPathToCache`.** `addPathToCache`
is also invoked by the background `scanWorker` threads
(`src/PathCacheManager.cpp:320, 460`); marking there would make the
recency set cross-thread and force a lock. Mark only in the
`onDirectoryChanged` slot, which is main-thread by construction. This is
the load-bearing thread-safety decision.

---

## Storage options compared (memory at ~2M entries)

| Option | What | Memory @ 2M | Locks | Cold-start recency | Verdict |
|---|---|---|---|---|---|
| (i) per-entry mtime field | grow storage by a 2–4 B mtime-as-epoch-days per path | see below | **yes** (written by bg scanWorker) | yes | rejected for v1 |
| **(ii) bounded touched set** | ring/hash of last N touched paths + coarse ts | **≤ ~1 MB, constant** | **none** (main-thread only) | no | **recommended** |
| (iii) hybrid | (ii) for live + coarse mtime buckets captured during scan | (ii) + scan cost | partial | yes | v2 |

**Option (i) memory math.** Two sub-cases:

- *Against the (future, non-existent) 12-byte `PathStore::Node`* — the
  brief's "~15 B/entry" baseline: a 2-byte epoch-days mtime is
  `+2/15 = +13.3 %` (= 4 MB at 2M); a 4-byte field is `+4/15 = +26.7 %`
  (= 8 MB). Non-trivial, and it still needs a lock because the scan
  workers write it.
- *Against the actual `QStringList` storage* — each entry already costs
  roughly the path string twice (`m_paths` + `m_lowerPaths`, ~2×(24 B
  header + 2 B/char)) plus a `QSet` node, i.e. **~300–400 B/entry**. A
  parallel `QVector<quint16>` mtime bucket adds 2 B × 2M = **4 MB (~1 %)**
  — cheap in absolute terms, but the killer is the **per-entry `stat()`
  at scan time** (`QDir::entryList` returns names only; mtime needs a
  `QFileInfo` syscall per entry across 200k–500k entries). That is the
  reason to defer it, not the RAM.

**Option (ii) memory math (the recommendation).** Bounded by
`kCapacity = 8192` slots regardless of change volume:

- `QHash<QString,qint64>`: key `QString` (~24 B header + ~2 B/char; a
  ~60-char path ≈ 150 B, but the QString is implicitly shared with the
  cache's copy where the pointer is reused) + 8 B value + ~16 B hash node
  ≈ ≤ 180 B/slot worst case.
- `QQueue<QString>`: one more shared `QString` ref ≈ ≤ 24 B/slot.

`8192 × ~200 B ≈ 1.6 MB` absolute worst case; realistically ≤ ~1 MB
because path `QString`s are shared with the cache. **Constant** — a
100k-file `git checkout` cannot grow it past `kCapacity` (oldest evicted).

---

## Ranking integration

The store's search already does the name-match filtering: `cache->search`
returns only substring/fuzzy candidates, and `fuzzyScore` returns 0 (and
the candidate is effectively dropped) for a non-match. **Recency is
applied only to results that already matched.** A non-match is never in
the candidate list, so recency can never surface it. This is the "must
still be a name match" guarantee, for free.

### Formula

Keep the displayed `score` in **[0,100]** unchanged (the delegate's score
chip depends on it — `SearchResultDelegate.cpp:381-392`). Introduce a
separate **sort key** that may exceed 100 and is used only for ordering:

```
nameScore  = fuzzyScore(path, query, root)          // 0..100, unchanged, displayed
bucket     = recencyBucket(path)                    // 0..3
bonus      = { 0:0, 1:+2, 2:+4, 3:+6 }[bucket]      // additive, bounded, small
sortScore  = nameScore + bonus                      // 0..106, sort-only
```

Sort by `sortScore desc`, then the existing tie-break `path.length() asc`:

```cpp
std::sort(results.begin(), results.end(), [](const SearchResult &a, const SearchResult &b){
    if (a.sortScore != b.sortScore) return a.sortScore > b.sortScore;
    return a.path.length() < b.path.length();
});
```

`SearchResult` gains one field: `int recencyBucket = 0;` (drives both the
sort bonus and the optional UI marker). `sortScore` can be a local, or a
second field — either is fine.

**Why additive, small, and sort-only (not a multiplier, not a primary
sort key):**

- *Not a primary sort key*: `sort by (bucket desc, score desc)` would let
  a **recent poor** match outrank an **old excellent** match — the exact
  failure the user does *not* want. Rejected.
- *Not a multiplier*: `score × (1+bucket)` scales large scores more than
  small ones and can blow the [0,100] display bound; harder to reason
  about. Rejected.
- *Additive + bounded* gives the precise property wanted:
  - **recent good beats old good** (equal `nameScore`, higher bonus wins
    → the headline gate);
  - **recent poor does NOT beat old excellent** as long as the relevance
    gap exceeds the max bonus (6). With `fuzzyScore` on a 0–100 scale, a
    poor match (~15–25) plus 6 still loses to an excellent match (~80).
  - Tunable: the four bonus values are the only knobs. If field testing
    shows recency too weak/strong, adjust `{2,4,6}` — do not change the
    structure.

---

## Age bucketing

Results must not reshuffle every second. Two rules enforce this:

1. Store a **timestamp**, derive the **bucket at query time** (never
   store the bucket — it would age incorrectly).
2. Buckets are **coarse** (`<1h`, `<1d`, `<7d`, older). Within a bucket
   the bonus is constant, so ordering is stable until an item actually
   crosses a boundary (at most 3 boundary crossings in its lifetime).
   Between keystrokes, a result's rank is deterministic.

Boundaries (1h / 1d / 7d) are chosen so the strongest boost covers "I
just did this in the last hour" — the dominant use case — while the day
and week buckets give a gentle, decaying nudge.

---

## Persistence interaction

**The touched set is in-memory only. Do not serialize it.** On restart it
is empty; cold-start ordering falls back to pure relevance (acceptable
for v1 — the user's "I just made this" scenario is a *running* session).
This keeps recency fully decoupled from any future index snapshot
(docs/210, which does not exist yet) and from any generation/listed pad
bits (also non-existent). If cold-start recency is later wanted, that is
Option (iii)/v2 via coarse mtime buckets captured during scan — a
separate, opt-in structure, still not entangled with generation bits.

---

## UI consideration (optional in v1)

`SearchResultDelegate` already draws a subtle score chip
(`SearchResultDelegate.cpp:381-392`) via a `ScoreRole`. Adding a
`RecencyRole` (from `SearchResult::recencyBucket`) lets the delegate draw
a small "·" dot or a low-saturation "new" chip for bucket ≥ 2 (touched
within a day), or a relative-time hint ("2m ago"). **Optional for v1** —
the ranking change alone satisfies the user's request; the marker is
polish. If added, keep it consistent with the existing chip styling
(small, low-saturation) per the delegate's documented conventions.

---

## What stays OUT of v1

- Per-entry mtime / any growth of the cache storage arrays.
- Scan-time (cold-start) recency ordering — deferred to v2 Option (iii).
- Persisting the touched set across restarts.
- Any change to the background `scanWorker` threads or to
  `addPathToCache` (keeps the design lock-free).
- Boost for *content* matches (ripgrep results) — out of scope.

---

## Adversarial review

**Recency drowning out relevance.** Mitigated by construction: bonus is
additive and capped at +6 on a 0–100 relevance scale, and it is a
sort-only key, never a primary sort dimension. A recent poor match cannot
overtake an old strong match. Gate: see the "poor recent < excellent old"
test below. *Residual risk:* if `fuzzyScore` values cluster very low
(e.g. all candidates score 5–12), +6 could dominate. Acceptable — in that
regime all candidates are weak matches and recency is a reasonable
tie-breaker. If it ever bites, lower the bonus table; structure unchanged.

**Clock skew / mtime in the future.** v1 uses **our own wall-clock at the
moment of the FSEvents callback**, not the file's mtime — a file with a
bogus future mtime is irrelevant. (v2 mtime path must clamp
`mtime > now → now` and treat epoch-0/invalid as bucket 0.) *Residual:*
if the system clock jumps backward, some timestamps read as "future" →
`ageMs < 0 < 1h` → bucket 3. Harmless (item is genuinely recent) and
self-heals within an hour. Optionally clamp `ageMs = max(0, ageMs)`.

**Mass touch (100k-file `git checkout` / `npm install`).** The set is a
ring bounded at `kCapacity = 8192`; a mass event evicts oldest and
saturates at N — **memory stays flat**. All N survivors are genuinely
recent, so boosting them is correct; and since bonus is small+additive, a
checkout of files that *don't match the user's query* never appears
(they're not candidates), and ones that *do* match get only a +6 nudge —
they don't drown a strong old match. Only ≤100 results surface per query
regardless. *Residual:* a mass event can evict a genuinely-relevant
earlier touch. Acceptable — that item simply reverts to bonus 0 (its
relevance score is unaffected). Graceful degradation, by design.

**Excluded-dir change noise.** `markTouched` is called **after** the
`m_excludeSettings->shouldExclude(entry)` gate in the new-entry loop
(`src/PathCacheManager.cpp:627`), so `node_modules`, `.git`, etc. never
enter the recency set even when they churn.

**Thread-safety.** `markTouched` (writer) runs only in
`onDirectoryChanged` (main thread, `QFileSystemWatcher` slot);
`recencyBucket` (reader) runs only in `performSearch` (main thread,
debounced). **Same thread → no lock, no race.** This is guaranteed *only*
because `markTouched` is kept out of `addPathToCache` (which the
background `scanWorker` calls). The design's one hard invariant:
**RecencyTracker is touched exclusively from the main thread.** If a
future change needs a background writer, add a `QMutex` first — do not
"just call markTouched from the worker."

**Memory bound proof.** `m_order.size()` is incremented once per new
key and decremented (via `dequeue`+`remove`) whenever it would exceed
`kCapacity`; `m_touchedAt.size() == m_order.size()` is an invariant
(insert/remove are paired). Therefore `|set| ≤ kCapacity = 8192` for all
inputs, independent of change volume → memory ≤ ~1.6 MB worst case,
constant. QED.

**Latency.** Per candidate: one `QHash` lookup (O(1)) + one integer add,
over the ≤100 already-materialized results. No extra pass over the 2M
cache. Search latency is unchanged within noise.

---

## Measured gates (an implementer must hit all)

1. **Headline behavior.** A path touched 10 s ago ranks **above** an
   identical-basename path touched 1 year ago, for a query matching that
   basename. (Unit test: seed two paths with equal `fuzzyScore`, mark one
   via `markTouched`, assert order.)
2. **Relevance floor.** A **recent poor** match (low `fuzzyScore`) does
   **not** outrank an **old excellent** match (high `fuzzyScore`) whose
   relevance gap exceeds the max bonus. (Unit test with crafted scores.)
3. **Non-match never surfaces.** A recently-touched path that does *not*
   match the query never appears in results. (Follows from applying boost
   post-filter; add a test asserting it.)
4. **Stability.** Re-running the same query within the same bucket window
   yields identical ordering (no per-second reshuffle).
5. **Memory bound.** After marking 100k distinct paths, the recency
   structure holds ≤ `kCapacity` entries and ≤ ~2 MB. (Assert
   `m_touchedAt.size() <= 8192`.)
6. **Latency unchanged.** `--bench` folder/file search p95 within noise
   (±5 %) of the pre-change numbers in `docs/030_performance_budget.md`.
7. **No thread regression.** Full suite green under
   `QT_QPA_PLATFORM=offscreen`; no new cross-thread access to
   RecencyTracker (grep: `markTouched` appears only in
   `onDirectoryChanged`).
8. **No public API break.** The only signature change is one added field
   on `SearchResult` and one new query method; existing 101 tests pass
   unchanged. Record the extension in `docs/050_porting_rules.md` under
   "Local extensions".

---

## Implementer checklist (build order)

1. Add `src/RecencyTracker.{h,cpp}` (main-thread-only; hash + FIFO ring
   as above). Unit-test bucketing and eviction in isolation first.
2. Own one instance from `PathCacheManager`; expose
   `int recencyBucket(const QString&) const` and
   `void markTouched(const QString&)` (or make RecencyTracker a small
   singleton the workers can reach — either is fine, keep it main-thread).
3. Call `markTouched` in `onDirectoryChanged` at the new-dir and
   new-file branches, **after** the exclude gate; **never** in
   `addPathToCache`.
4. Add `int recencyBucket = 0;` to `SearchResult`. In both
   `FolderSearchWorker::performSearch` and
   `FileSearchWorker::performSearch`, set it from
   `recencyBucket(path)` and sort by `nameScore + bonus(bucket)` then
   path length. Keep the displayed `score` unclamped-from-recency
   (still 0..100).
5. (Optional) `RecencyRole` in `SearchResultModel` + a subtle marker in
   `SearchResultDelegate` for bucket ≥ 2.
6. Add the gate tests (§1–5) to the relevant QTest class; run `--bench`
   for §6; run `./br --test` for §7–8.
</content>
