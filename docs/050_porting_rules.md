# Lift History — Where the Code Came From

This app started as a lift from `../maude-cp-v3`. We're now independent
— this doc records what was lifted and how it was modified, so future
contributors know which files have an upstream origin (and where the
authoritative documentation for the original algorithms lives).

There is **no backport obligation** to upstream. We change whatever
makes this app better.

## Lifted files (origin in `../maude-cp-v3`)

| Concern | Original upstream path |
|---|---|
| Cache + BFS scan + FSEvents | `src/PathCacheManager.{cpp,h}` |
| Search worker | `src/FolderSearchWorker.{cpp,h}` |
| Search field widget | `src/SearchField.{cpp,h}` |
| Exclude settings + dialog | `src/ExcludeSettings*.{cpp,h}` |
| Path selector v2 (façade + state + UI + popup + keyhandler + adapter) | `src/PathSelector/*` |
| Folder browser dialog | `src/FolderBrowserDialog.{cpp,h}` |
| Support: style / theme / config / icons | `src/SwiftUIStyle.*`, `src/ThemeManager.*`, `src/MaudeConfig.*`, `src/IconRegistry.*` |
| Tests | `tests/PathCacheManagerTest.{cpp,h}`, `tests/SearchFieldTest.{cpp,h}`, `tests/ExcludeSettingsTest.{cpp,h}` |

For the original algorithms (BFS scan, FSEvents handling, 5-state
PathSelector machine, multi-term AND search) the authoritative
write-ups are mirrored under `docs/qt-reference/`. Read them before
changing the corresponding code — they encode constraints the lifted
code follows.

## Stripped on import

- **inotify** branch (Linux) — gone. macOS only.
- **`ReadDirectoryChangesW`** branch (Windows) — gone.
- Any `#ifdef Q_OS_LINUX` / `#ifdef Q_OS_WIN` block — gone.
- `MaudeConfig::settingsFilePath()` indirection in `ExcludeSettings` —
  replaced with plain default `QSettings` (org/app set in `main.cpp`).
- Icon resources reduced from upstream's full catalogue to just the
  SVGs the dialog actually uses (home, chevron-up, gear, folder,
  folder-open, eye).

## Local extensions

Functionality the standalone app adds on top of the lift. These live
mostly in `FolderBrowserDialog` and `main.cpp`.

| File | Local change |
|---|---|
| `src/main.cpp` | Priority-queue startup scan driven by the favorites list (default favorite first, then sidebar order). See `docs/todos.md` TODO 3. |
| `src/FolderBrowserDialog.cpp` | Two open buttons (`Open in Finder` + `Open with App`) instead of one `Choose`. Dialog stays open after open; never closes via Esc. Favorites sidebar replaces the modal-picker framing. Tree's `keyboardSearch` suppressed via event filter so printable keys route to the search field. Extra chords (⌘F, ⌘L, ⌘H, ⌘↑). Persistent shortcut hint line. |
| `src/SearchField.cpp` | Added `setFocusProxy(m_lineEdit)` so `searchField->setFocus()` actually reaches the editable widget. Bug fix. |
| `src/ExcludeSettings.{h,cpp}` | Added `QReadWriteLock` guarding `m_patterns` + `m_enabledPatterns`. Required for thread-safe access from `PathCacheManager` workers. See `docs/120_qt_threading.md`. |
| `src/PathStore.{h,cpp}` | **New (2026-07-21).** Compact tree-arena path storage shared by `PathCacheManager` + `FileCacheManager` — 12-byte nodes over one UTF-8 name arena, ~32 B per entry vs. ~730 B for upstream's triple-container layout (measured: G1 test in `PathStoreTest`). Search is two linear passes (per-segment term scan with ASCII fast path, then forward parent-mask propagation). Deletions tombstone nodes; name bytes are reclaimed only by full rescan. Design: `docs/200_pathstore_redesign.md`. |
| `src/PathCacheManager.cpp` | Path-level system excludes (`/System`, `/private`, `/dev`, `/Volumes`, …) so `/` can be scanned without bloating the cache with system files. `setShowHidden` is now a no-op — the cache always indexes hidden folders; the eye toggle is presentational only. See `docs/todos.md` TODO 4. Storage internals replaced by `PathStore` (2026-07-21): the scan queue carries store nodes, each directory listing is ingested in one atomic batch, and the FSEvents watcher diffs via `childrenOf()` instead of full-cache sweeps. |
| `src/FileCacheManager.{h,cpp}` | Storage internals replaced by the shared `PathStore` (2026-07-21). Public API, `$HOME` scope guard, two-tier caps and signals unchanged; `clear()` tombstones only file entries. |
| `src/PathStore.{h,cpp}` + `src/PathCacheManager.{h,cpp}` + `src/main.cpp` | **Persistent index (2026-07).** `PathStore::saveTo/loadFrom` (MSIX raw-blob snapshot, live nodes only, `QSaveFile`) + generation mark-and-sweep (`beginScanGeneration`/`sweepStale`, using the Node's spare `pad` bits — zero extra memory). `PathCacheManager::indexFingerprint` (excludes + caps + format version), `saveSnapshot`/`tryLoadSnapshot`, and `finishScan` sweeps+saves per completed root. `main.cpp` loads the snapshot before scanning and saves on quit; the `loadedFromSnapshot` flag drives the dialog's "Ready (cached) … verifying…" label. Design + status: `docs/210_persistent_index.md`. |
| `src/FolderSearchWorker.{h,cpp}` | Added `setIncludeHidden(bool)` + static `pathIsHidden(path)`. Filters cache results that contain hidden path segments. |
| `assets/icons.qrc` | Minimal subset of upstream's icon resource set. |
| `assets/macos-search.icns` | New app icon. |

## Deliberate search-semantics change (PathStore, 2026-07-21)

Upstream matched every query term against the **whole lowercased
absolute path string**. PathStore matches every term against
**individual path segments** (ancestor folder names count, so
`projects readme` still finds `/a/projects/x/README.md`). Two visible
deltas:

- **`/` in a query is now a term separator.** `projects/readme` behaves
  exactly like `projects readme`. Upstream required the characters
  around a `/` to be literally adjacent (`o/pro` only matched
  `…o/pro…`); now each side just has to appear in *some* segment — a
  strictly more forgiving match for the way people type paths.
- **A single term never crosses a `/` boundary** — true upstream too
  (the `/` character intervened), now guaranteed structurally.

Two scan-behavior deltas, both strictly less wasted work:

- Cap-blocked children are no longer descended into (upstream kept
  BFS-walking the whole disk while adding nothing).
- Expanding directly *into* a path-level-excluded root (e.g.
  `/System/x`) stops at the root instead of listing-and-discarding
  every child.

## License headers

Same as upstream `maude-cp-v3` LICENSE for the lifted modules, until
the distribution path is decided. Local extensions inherit the same.
