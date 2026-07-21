# 210 — Persistent index: warm start + reconciliation

**Goal:** app start → searchable from the last known index in ~0.2 s;
a background rescan brings the index back in sync with reality; no new
memory cost; as little code as possible.

## Concept

### 1. Snapshot file

`~/.macos-search/index-v1.bin`, written with `QSaveFile` (atomic):

- Header: magic `MSIX`, format version, entry counts, and a
  **fingerprint** — a hash over everything that shapes the index:
  enabled folder+file exclude patterns, the path-level exclude list
  (including `$HOME`), and both caches' cap values.
- Body: raw dump of `PathStore` — the POD node array and the name
  arena, plus per-kind counts. 2 M entries ≈ 32 MB → loads in well
  under a second with two reads. No per-entry serialization loop.

### 2. Save / load points

- **Save** in `finishScan()` (scan thread, store read-lock) after every
  completed root, and on `aboutToQuit`. Saving 32 MB takes ~0.1 s —
  cheap enough to not need debouncing.
- **Load** at startup, before the `ScanScheduler` starts, on the main
  thread (no workers exist yet — no locking hazard). Version or
  fingerprint mismatch → ignore snapshot, cold scan exactly as today.
  Corrupt/truncated file → same. The snapshot is a pure accelerator;
  deleting it is always safe.

### 3. Update after start — reconciliation

The **normal priority rescan runs unchanged** (Desktop → Downloads →
Documents → Dropbox → home → favorites). Two mechanisms converge the
loaded snapshot to reality:

- **Additions:** already free. `ingestListing()` dedupes against the
  store's existing children; anything new on disk gets added as the
  walk reaches it.
- **Deletions: generation mark-and-sweep**, using the 16 spare bits
  already in each `PathStore::Node` (the `pad` field — zero extra
  memory): 15-bit scan generation + 1 "listed" bit.
  - Each scan run gets a fresh generation. `ingestListing(dir)` stamps
    the generation on the directory node (with the *listed* bit) and on
    every child it sees — existing or new.
  - When a root's scan completes, `sweepStale(root, gen)` tombstones
    exactly those nodes whose **parent was listed this generation but
    which were not seen** in that listing. Local consistency: "parent
    was read, child wasn't there → child is gone."
  - UI stays honest during reconciliation: label shows
    `Ready (cached) — N folders · M files · verifying…` until the first
    post-load scan chain completes, then plain `Ready`.

### 4. What deliberately stays out (v1)

- **mtime-pruned refresh** (stat dirs, list only changed ones). Would
  cut the ~88 s reconcile walk to ~20-40 s, but adds per-folder mtimes
  to the format and a second walk mode. v2 candidate if the background
  walk ever bothers anyone — it's invisible with a warm UI.
- **Rebuild-into-fresh-store + atomic swap.** No deletion logic needed,
  but requires double-buffering both managers behind an indirection.
  More machinery than the two pad-bits.
- **Incremental journal** (append adds/removes). Complexity for no
  user-visible win at 0.1 s save cost.

## Review (adversarial pass before building)

- **Skipped-subtree hazard (the big one):** scanning `~` skips subtrees
  under `m_completedRoots` (e.g. Desktop, scanned minutes earlier). A
  naive "sweep everything under root not seen this generation" would
  tombstone all of Desktop. The parent-was-listed rule is immune: the
  skipped subtree's parents were never listed this generation, so their
  children are untouched. This rule is the load-bearing design decision.
- **Leaf nodes (bundles, symlinks, cap-blocked):** bundles and symlinks
  appear in their parent's listing → stamped like any child; they are
  never listed themselves → their *own* children (none) are never swept.
  Consistent.
- **Excludes changed while app closed:** fingerprint mismatch → cold
  scan. No partial migration logic.
- **Crash mid-save:** `QSaveFile` never replaces the old snapshot with
  a truncated one.
- **Generation wraparound:** 15 bits = 32 k scans per process lifetime;
  generation state is in-memory semantics only (snapshot stores it but
  load can normalize to 0). Non-issue.
- **Concurrent favorites scans:** scans are serialized by the
  scheduler/`m_scanThread` today; sweep happens in `finishScan` on the
  scan thread. No new races. Store mutations remain behind its lock.
- **Tombstone accumulation across warm starts:** tombstones are dropped
  at save time (write only live nodes, remapping parent indexes) OR
  kept and compacted on the next cold rescan. v1: write live nodes only
  — the remap is a simple O(n) pass with an index translation array.
- **Watcher during reconcile:** unchanged behavior; watcher diffs
  against `childrenOf()` which reflects whatever state the store has.

## Measured gates

- Warm start: snapshot load ≤ 1 s for 2 M entries; UI label shows
  cached counts immediately.
- Reconcile correctness (tests): file deleted while "app closed" is
  gone after the covering scan completes; file created while closed
  appears; subtree scanned as an earlier root is NOT swept by the later
  home scan (the hazard test).
- Full suite stays green; no public-API changes to the managers.
