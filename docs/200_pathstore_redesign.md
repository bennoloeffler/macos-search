# 200 — PathStore redesign: 20× memory reduction

**Goal: 20× reduction in cache memory, MEASURED and test-gated, with LESS
code than today.** Baseline measured 2026-07-21 on this machine
(`/usr/bin/time -l … --bench --bench-root ~/projects`):

| Metric | Baseline (main @ 99e7811) |
|---|---|
| Entries (60,319 folders + 450,324 files) | 510,643 |
| Peak RSS | 445 MB |
| **Bytes per cached entry (end-to-end)** | **~730 B** |
| Extrapolated at caps (1 M folders + 5 M files) | ~4.4 GB |

Root cause: every entry stores its **full absolute path 3×**
(`m_paths` QString UTF-16, `m_lowerPaths` lowercased copy, `m_pathSet`
hash node), duplicated across two near-identical managers. The actual
information per entry is one path *segment* (~15–20 bytes UTF-8).

## Target design: one shared `PathStore`

One new class replaces the storage internals of BOTH `PathCacheManager`
and `FileCacheManager` (their public APIs stay; they become thin
facades — DRY, net code should go DOWN).

```cpp
// src/PathStore.h  — the ONLY place path data lives.
class PathStore {
public:
    enum Kind : quint8 { Folder, File };

    // Nodes. Invariant: parent index < child index (append-only).
    // Deletion = tombstone flag; indexes are stable forever.
    // clear() is the only shrink operation (rescan path).
    qint32 addChild(qint32 parent, QByteArrayView utf8Name, Kind kind);
    qint32 findOrCreatePath(const QString &absPath);   // rare path (roots, watcher)
    qint32 find(const QString &absPath) const;          // -1 if absent
    QString pathOf(qint32 node) const;                  // materialize on demand
    QList<qint32> childrenOf(qint32 node) const;        // linear scan — fine
    void markDeletedRecursive(qint32 node);
    void clear();

    int count(Kind k) const;            // live (non-tombstoned) entries
    qint64 bytesUsed() const;           // exact accounting — the measured gate

    // Search: every term must appear case-insensitively in SOME segment of
    // the path (ancestor names count). '/' and whitespace both split terms.
    QStringList search(const QString &query, Kind kind,
                       const QString &rootPath, int maxResults) const;

private:
    struct Node {                       // 12 bytes
        qint32  parent;                 // -1 for root nodes
        quint32 nameOff;                // offset into m_names
        quint8  nameLen;                // macOS names ≤ 255 UTF-8 bytes
        quint8  flags;                  // Kind | Deleted | HasNonAscii
        quint16 pad;
    };
    std::vector<Node> m_nodes;
    QByteArray        m_names;          // ONE arena: concatenated original UTF-8 names
    mutable QReadWriteLock m_lock;
};
```

**Per-entry cost: 12 B node + ~18 B name ≈ 30 B, ≤ 36 B with growth
spare → ≥ 20× vs 730 B.** No lowered copy, no per-entry QString, no
per-entry hash node.

### Search algorithm (two linear passes, no index needed)

1. Lowercase the query; split on whitespace AND `'/'` → terms (cap 8).
2. **Pass A** — per term, scan each node's name span in `m_names`:
   - pure-ASCII name (flag set at insert) + ASCII term → byte scan with
     `tolower` compare (fast path, ~95 % of names);
   - otherwise → `QString::fromUtf8(name).toLower().contains(term)`
     (Unicode-correct — umlauts matter).
   Result: `uint8 mask` per node (bit i = name contains term i).
3. **Pass B** — single forward loop exploiting parent < child:
   `mask[i] |= mask[parent]`; same trick computes `underRoot[i]`.
   A node matches when `mask == allTerms && underRoot && kind && !deleted`.
4. Materialize matched paths via `pathOf()` until `maxResults`.

Transient cost: 1 byte × nodes (≈ 6 MB at full caps). Pass A scans the
~110 MB name arena once per term — faster than today's UTF-16 scan of
730 MB.

### Deliberate semantic changes (document in 050_porting_rules.md)

- A term no longer matches ACROSS a `/` boundary (`"o/pro"`). `'/'` in
  a query now acts as a term separator. Multi-term AND across ancestors
  (`"projects readme"`) works exactly as before.
- Watcher deletions tombstone nodes; name bytes are reclaimed only by
  full rescan. Bounded, acceptable.

### What stays exactly as-is

- Public API of both managers: `search()`, `cachedPaths()`,
  `cachedFiles()`, `folderCount()`, `fileCount()`, `contains()`,
  `addFile()`, `removeFilesUnder()`, caps/signals, `ExcludeSettings`,
  `FolderSearchWorker` scoring/hidden-filter, subfolder-suppression
  (runs on the ≤ 100 materialized results).
- All 21 existing test classes must stay green — they are the
  regression gate.

### Scan-path integration

`scanWorker()` resolves the current directory to a node ONCE, then
`addChild(dirNode, name, kind)` per entry — no per-entry path strings,
no per-entry dedupe lookups (directory-level dedupe already exists via
`m_completedRoots` + an in-scan visited set). The per-entry
`m_pathSet.contains()` layer disappears. Watcher diffs use
`childrenOf()` instead of full-cache sweeps (also kills the
main-thread O(6 M) sweep per fs event).

## Measured gates (all must hold)

- **G1 (unit, hard-fail):** `bytesUsed()/entries ≤ 36 B` on 100 k
  synthetic entries with realistic names → ≥ 20× vs baseline 730 B.
- **G2 (bench):** steady-state RSS delta (task_info after scan+queries
  minus at startup) per entry ≤ 80 B on `--bench-root ~/projects`.
- **G3 (regression):** all existing tests green; search results for a
  fixture tree identical to old semantics minus the documented `/`
  boundary change.
- **G4 (perf):** `--bench` p95 search latency not worse than baseline.

## Work blocks (parallel worktrees)

- **Block 1 (priority): PathStore** — tests first
  (`tests/PathStoreTest.cpp`), then implementation, then swap manager
  internals. Biggest block.
- **Block 2: scan lifecycle honesty** — stop BFS descent when caps are
  reached (today it walks the whole disk for nothing), status label
  shows folders + files, cap defaults down (files soft 1 M / ceiling
  2 M; folders soft 500 k / ceiling 1 M),
  `malloc_zone_pressure_relief` after scan completes.
- **Block 3: measurement harness** — mach `task_info` RSS
  (start/after-scan/peak) in `--bench` JSON; `scripts/mem-compare.sh`
  runs two binaries on the same root and prints B/entry + ratio.

Merge order: 3 → 2 → 1 (small first; Block 1 rebases and must pass
G1–G4 through Block 3's harness).
