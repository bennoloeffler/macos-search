# Feature plan — File search + content search

Two major features layered on the existing folder-search subsystem.
Captures the analysis, the decisions already made, and every open
question. Used as the working doc until a final plan lands.

Status as of 2026-05-19: **IMPLEMENTED end-to-end**. All Phase A/B/C
tasks landed. 309 tests green (`./br --who=claude --test`),
up from 193 baseline.

## Implementation summary

- `src/ExcludeSettings.{h,cpp}` — folder + file patterns, INI migration alias.
- `src/FileCacheManager.{h,cpp}` — separate file index, 500k cap, FSEvents-aware.
- `src/FileSearchWorker.{h,cpp}` — mirror of FolderSearchWorker, shares fuzzy score.
- `src/PathCacheManager.cpp` — scanWorker now feeds both caches in one BFS walk; opaque-bundle skip (.app, .photoslibrary, .imovielibrary, .musiclibrary, .tvlibrary, .aplibrary).
- `src/ContentSearchSettings.{h,cpp}` — threshold (default 1000, range 100–5000), size cap, extension blacklist, file-cache cap.
- `src/RipgrepRunner.{h,cpp}` — QProcess wrapper, streams JSON, supports regex/fixed-string, cancellable.
- `src/EditorLauncher.{h,cpp}` — VS Code detection + `code --goto file:line` invocation.
- `src/ExcludeSettingsDialog.{h,cpp}` — now a two-tab dialog (Folders | Files).
- `src/FolderBrowserDialog.{h,cpp}` — segmented control (Folders/Files/Both, default Both, persisted), score-interleaved merged results, file rows with file glyph + extension chip, "Inside contents:" field gated by threshold, Regex checkbox + help popover (10 examples), expandable per-file content matches, Will-open footer reflects `:line`, ⌥⏎ chord for "open at line in VS Code", min width raised to 820.
- `third_party/rg/` — vendor directory + README documenting the binary refresh procedure. CMake bundles the binary into `Contents/Resources/rg` if present; runtime falls back to system `rg`.

## Test growth

| Suite | Tests |
|---|---|
| ExcludeSettingsTest | 51 (was 31) |
| PathCacheManagerTest | 13 |
| SearchFieldTest | 16 |
| PathSelectorStateTest | 8 |
| FolderBrowserDialogTest | 9 |
| UserInteractionTest | 25 |
| UsabilityTest | 47 |
| CacheStrategyTest | 9 |
| AutostartTest | 15 |
| GlobalHotkeyTest | 8 |
| PreferencesDialogTest | 12 |
| **FileCacheManagerTest (new)** | 19 |
| **FileSearchWorkerTest (new)** | 11 |
| **ContentSearchSettingsTest (new)** | 15 |
| **RipgrepRunnerTest (new)** | 16 |
| **EditorLauncherTest (new)** | 6 |
| **FileSearchIntegrationTest (new)** | 7 |
| **FileSearchUiTest (new)** | 13 |
| **ContentSearchE2ETest (new)** | 9 |
| **Total** | **309** |

## Known follow-ups

- **Ripgrep binary** is not yet committed to `third_party/rg/macos-arm64/rg`. Drop the official ripgrep 14.x arm64 binary there (procedure in `third_party/rg/README.md`) before shipping a release; until then the bundle relies on a system `rg` on `$PATH`.
- **`--bench` CLI** for measuring scan + search latency (Phase A4-A5) was scoped but not built — the test layer covers correctness; a benchmark harness writing into `docs/030_performance_budget.md` is the next item for Phase D.
- **Trigram index** for filename search (Phase D D1) was scoped to "only if measurements force it"; deferred until benches exist.

---

---

## TL;DR

1. **File search** alongside folder search. Same scan walk, **separate
   in-memory file cache**, same lock pattern. Segmented toolbar control
   `Folders | Files | Both`. Default `Both`.
2. **Content search** as a second query field that *activates* once the
   filename filter narrows to ≤ N files (default `N = 1000`,
   configurable). Engine: bundled `ripgrep`. Streams matches into the
   results list. Raw-text only in v1 — no PDF/DOCX/XLSX extraction.

---

## What today's pipeline does (and where it pushes back)

Today's flow:

```
PathCacheManager (folders only)
  └─ scanWorker uses QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable | QDir::Hidden
  └─ QStringList m_paths + QSet m_pathSet  (~80k entries on Benno's $HOME)
  └─ search() applies subfolder-of-existing-match suppression
FolderSearchWorker
  └─ 50 ms debounce, applies fuzzyScore, returns top 100 by score
FolderBrowserDialog
  └─ QListWidget for results, QFileSystemModel for the empty-query tree
ExcludeSettings
  └─ Folder-name patterns (one list), QReadWriteLock-guarded
```

Structural hazards for adding files:

- File count is **10–30×** folder count. Naive merge into one cache
  blows memory and breaks the 50 ms search budget.
- The `search()` "subfolder of an existing result" suppression rule is
  **wrong for files** — `foo/README.md` and `foo/Makefile` are not
  redundant.
- `ExcludeSettings` matches folder-name patterns only. Files need their
  own pattern list (`*.pyc`, `.DS_Store`, etc.).
- Threading is the #1 historical hazard (see `120_qt_threading.md`).
  Anything new must follow the `QReadWriteLock` pattern.

---

## Architecture decisions (locked)

### One scan walk, two caches

Single BFS walk populates both a folder cache (existing
`PathCacheManager`) and a new file cache (`FileCacheManager`). Each
scan worker, when enumerating a directory:

1. `QDir::entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable | QDir::Hidden)` → folders.
2. `QDir::entryList(QDir::Files | QDir::NoDotAndDotDot | QDir::Readable | QDir::Hidden)` → files.

Both lists pass through their respective exclude predicate, then go
into their respective cache. No second walk.

**Why two caches, not one:**
- Folder-search 50 ms budget stays intact.
- File cache can grow into hundreds of thousands without slowing
  folder search.
- File-specific behavior (no subfolder suppression, different excludes,
  cap) is isolated.

**Why not Spotlight (`mdfind`):** `docs/040` says we own our cache.
`-onlyin` doesn't compose with favorite scopes. Tail latency on cold
queries is unpredictable.

**Why not on-demand file scan:** breaks the "instant after warm cache"
contract.

### File-cache safety cap

Default cap: **500 000 entries**. When hit, scan stops adding to the
file cache. Toolbar surfaces an orange status:

> File index cap reached (500 000). Tighten exclude rules or raise the
> cap in Preferences.

Folder scan continues. Cap is configurable in Preferences. Fail-loud,
not fail-silent.

### Same `QReadWriteLock` discipline

`FileCacheManager` and any new settings class get the same
shared-read / exclusive-write pattern that fixed the 2026-05-18 crash
in `ExcludeSettings`. No exceptions.

---

## Feature 1 — File search

### UX

**Segmented control** on the toolbar, next to the eye/gear buttons:

```
[ Folders | Files | Both ]
```

- **Default: `Both`** (confirmed).
- Persisted in `QSettings("Maude", "FolderBrowser")` under
  `searchMode` (string: `folders` / `files` / `both`).
- Acts as a *display filter on top of unified results*, not three code
  paths. Both caches are always populated; the toggle decides what is
  surfaced.

**Match target:** whole absolute path, multi-term AND, case-insensitive
— identical to today's folder rule. The existing
`fuzzyScore(path, query, rootPath)` formula already biases basename
matches via `basename_bonus` and works on file paths unchanged.

**Visual differentiation in the results list:**
- Folder rows: existing folder glyph.
- File rows: file glyph + a small dim extension chip (`.pdf`, `.cpp`,
  `.docx`).
- Score badge unchanged.

**In "Both" mode**: results are interleaved by score (highest first).
Folders and files compete on the same axis. Tiebreaker: shorter path
wins (existing rule).

**Empty-query state in tree view:** unchanged — show the existing
`QFileSystemModel` folder tree even in Files / Both modes. The tree
is for *navigation*, not for results. The mode toggle only affects the
search-results list.

**Open semantics:** no new buttons or labels. Both existing buttons
work correctly for files:

| Kind | `Open with App` / ⏎  | `Open in Finder` / ⌘⏎ |
|---|---|---|
| Folder | Open folder (Finder window) | Reveal in parent |
| File | Open in associated app | Reveal in Finder |

### Excludes — folder vs file patterns

`ExcludeSettings` is split internally into two lists. INI migration:
if `patterns=` exists and `folderPatterns=` does not, copy.

**Folder patterns** (defaults unchanged — these are today's defaults):
```
node_modules, .venv, venv, __pycache__, .git, build, dist, out,
.gradle, .idea, .vscode, vendor, .tox, target, .cache, .npm, .yarn,
*.egg-info
```

**File patterns** (new, defaults):
```
.DS_Store, .localized, Thumbs.db, desktop.ini,
*~, *.swp, *.swo,
*.pyc, *.pyo, *.class, *.o, *.a,
Icon\r
```

Images, video, archives **stay in by default** — users do search for
`vacation_2024.mp4` by name.

`ExcludeSettingsDialog` gets a two-tab layout: **Folders** | **Files**.

### Live updates

`onDirectoryChanged` already runs when FSEvents fires. Extend the diff
to include the directory's file list (`QDir::Files`), same shape as
the existing folder diff.

### Search latency

At 500k entries: estimated 80–150 ms per keystroke with the existing
`O(N) .contains()` scan. Inside the 200 ms steady-state budget but
near the edge. Mitigations, in order:

1. Ship the simple parallel scan (`QtConcurrent::filtered`) first.
2. Add `--bench` CLI (already a tracked open question in `090`) to
   measure.
3. **Only if measurements demand it**, add a trigram index on
   lowercased basenames (~3 MB for 500k filenames, search drops
   <10 ms).

### Tests

Net-new (~30 tests):
- `FileCacheManagerTest` — mirror of `PathCacheManagerTest`.
- `FileSearchWorkerTest` — basename match, multi-term AND, scoring,
  hidden-file presentation filter.
- `ExcludeSettingsTest` — extend with a `fileBranch` group.
- `UserInteractionTest` — segmented control, mode persistence, Files
  mode opens with associated app on ⏎.

---

## Feature 2 — Content search ("killer feature")

### UX

**Second `QLineEdit`** under the existing filename query, above the
results area:

```
Search for:       [ projects readme         ]   <- existing filename query
Inside contents:  [ ___________________     ]   <- new
                  (1234 files match — narrow to enable content search)
```

State machine for the content field, gated on filename-result count:

| Filename matches | Content field | Footer hint |
|---|---|---|
| 0 | disabled, empty | "No files match the filter." |
| 1 ≤ N ≤ threshold | enabled | "Searching contents in {K} files…" (running) / "Found {M} matches in {K} files" (done). |
| > threshold | disabled, dim | "Narrow filename filter to ≤ {threshold} files to enable content search." |

**Threshold:** default **1000** (confirmed). Configurable in
Preferences. Range: 100–5000.

### Engine: bundled ripgrep

`rg` shipped in `Contents/Resources/rg` (~8 MB binary; macOS-only;
confirmed bundling).

- CMake change: `install(FILES rg DESTINATION ...)`.
- Discovery order: user-installed `rg` on `$PATH` first, then bundled
  fallback. Lets power users pin a newer version.
- Code change: one `RipgrepRunner` `QProcess` wrapper class.
- Invocation: `rg --json --max-filesize 5M --max-count 20 --threads N -e term1 -e term2 --files-from -` then write the candidate file paths to stdin. JSON output streamed and parsed line by line into `QListWidget` rows.

Confirmed v1: **no PDF / DOCX / XLSX content extraction**. ripgrep
treats them as binary and silently skips. Surface that honestly with a
per-row note: `(binary/zipped format — text contents not searched)`.

### Content-search excludes (new `ContentSearchSettings`)

Separate from filename excludes. Same `QReadWriteLock` pattern.

**Hard-skip extensions** (defaults):
```
.png .jpg .jpeg .gif .webp .heic .heif
.mp4 .mov .m4a .mp3 .wav .ogg
.zip .gz .bz2 .xz .7z .tar .rar
.dmg .pkg .ipa .iso
.dylib .so .o .a .class .pyc .pyo .wasm .bin .dat
.key .keychain
```

**Hard-skip size:** files > 5 MB. (ripgrep `--max-filesize 5M`.)
Configurable.

**Hard-skip by null-byte heuristic:** rely on ripgrep's built-in.

### Results layout in content mode

When content search is active, the results list switches from
"file per row" to **"file per row with expandable matches"**.

- Top-level row: file path + match-count badge.
- Expand (click / →) reveals child rows: `Line {N}: {snippet}`.
- Snippet truncated to ~200 chars; matched span highlighted (reuse
  the existing purple-highlight code from filename matches).
- Per-file match cap: 20 matches (controls UI size; ripgrep
  `--max-count 20`). Tail shown as `+M more`.
- Keyboard:
  - ↑/↓ navigate.
  - →/← expand/collapse.
  - ⏎ open in associated app.
  - ⌘⏎ reveal in Finder.
  - **⌥⏎ open at line N in VS Code** (see below).

### "Open at line N" — VS Code auto-detection (confirmed)

Detection order:
1. `code` on `$PATH` (typical Homebrew or VS Code "Install 'code' command in PATH" install).
2. `/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code`.
3. `/Applications/VSCodium.app/Contents/Resources/app/bin/codium` — open question, see below.

Invocation: `code --goto "{file}:{line}:{col}"`. Fallback when not
found: ⌥⏎ degrades to plain `open file`. Surface in a tooltip on first
use: `⌥⏎ opens in VS Code at the matched line.`

### Performance & cancellation

- Debounce content query: **200 ms** (longer than the 50 ms filename
  debounce — content terms are more deliberate).
- ripgrep streamed via `QProcess::readyReadStandardOutput`. Results
  paint incrementally. UI never blocks.
- On each new keystroke: `kill()` the in-flight `QProcess`, spawn a
  fresh one. Standard cancellation pattern.

### Concurrency

Mirror of `ExcludeSettings` lesson: the filename-result snapshot
passed to ripgrep is a **copy taken under a read lock**. The
`QProcess` thread does not see the live cache.

### Tests (~25 new)

- `RipgrepRunnerTest` — process spawn, JSON parse, cancel, missing
  binary, oversize file, multi-term AND.
- `ContentSearchSettingsTest` — extension blacklist, size cap, max-files cap, threshold migration.
- `UsabilityTest` extensions — content field state at boundaries,
  expand/collapse, ⌥⏎ behavior with and without `code` on PATH.

---

## Phased build sequence

Ordered so each phase is releasable. Tests gate each phase.

### Phase A — Foundations (no user-visible change)

1. Split `ExcludeSettings` storage into `folderPatterns` + `filePatterns` with INI migration.
2. Add `--bench` CLI flag — JSON timings for scan and search latency. Needed to validate Phase B's perf claims.

### Phase B — File search v1

3. `FileCacheManager` + scan-walk extension. Cache-layer tests.
4. `FileSearchWorker` (shares `fuzzyScore` with folder worker).
5. UI: segmented `Folders | Files | Both`, file glyph + extension chip, two-tab `ExcludeSettingsDialog`.
6. FSEvents wiring for file diffs.
7. Cap + status surfacing.

### Phase C — Content search v1 (the killer feature)

8. Bundle `rg` binary, CMake install rule, `RipgrepRunner` wrapper.
9. `ContentSearchSettings` (size cap, ext blacklist, max-files cap).
10. Second query field with state-machine gating.
11. Expandable result rows with line snippets + highlighting.
12. VS Code detection + ⌥⏎ "open at line".

### Phase D — Polish (defer)

13. Trigram index for filenames — only if Phase B bench shows >100 ms search latency on real load.
14. Format-aware content extractors (PDF/DOCX/XLSX) — opt-in Preferences toggle. Separate Phase.
15. Persistent on-disk cache snapshot for faster cold start.

---

## Decisions confirmed (2026-05-19)

**Round 1** — architectural baseline:

| # | Question | Decision |
|---|---|---|
| 1 | Default segmented-control mode | **Both** |
| 2 | File-cache cap default | **500 000** |
| 3 | Content-search threshold | **Configurable; default 1000; range 100–5000** |
| 4 | ripgrep distribution | **Bundle in app** |
| 5 | "Open at line N" | **Auto-detect VS Code; fall back to `open file`** |
| 6 | PDF/DOCX content search in v1 | **No formats — raw text only** |

**Round 2** — UX + delivery details:

| # | Question | Decision |
|---|---|---|
| F1 | "Both" mode ordering | **Interleave by score** (folders + files compete on the same axis; tiebreaker = shorter path). |
| F2 | Eye toggle applies to files | **Yes** — symmetric with folders. Cache always indexes hidden; eye toggle is presentational only, same as today. |
| F8 | macOS library bundles | **Skip as opaque** — `.photoslibrary`, `.imovielibrary`, `.musiclibrary`, `.tvlibrary`, `.aplibrary` join `.app` in the opaque-bundle list. |
| F11 | Default file-exclude patterns | **Approved as listed** above. |
| F13 | File-search latency target v1 | **Relaxed**. Ship the simple parallel path. **Mandatory: measure with `--bench` and write the actual numbers into `docs/030_performance_budget.md` as part of Phase B.** Trigram index only if numbers force it. |
| C2 | Regex in content query | **Toggle: `Regex` checkbox next to the content field. Default OFF (plain text).** When toggled ON, show a small `?` button that opens a popover with **10 useful examples** (case-insensitive, word boundary, capture group, alternation, line anchor, char class, quantifier, negation, escaped dot, multi-line). |
| C4 | Will-open footer for line matches | **Yes** — when a content-match child row is selected, footer reads `Will open: /abs/path/file.cpp:42`. |
| C6+C7 | ripgrep packaging | **Check the binary into the repo** under `third_party/rg/macos-arm64/rg` (+ `macos-x86_64/rg` if/when a universal build is needed). Pinned by being in git — no permanent download. Updates are deliberate commits. Code-sign at app-bundle time. |
| C9 | Editor detection scope | **VS Code only in v1.** No Cursor / VSCodium / Windsurf. |
| X4 | Toolbar real estate | **Dialog minimum width raised to 820 px.** Go larger if the UX gets more space and elegance — don't cram the new controls. |

---

## Remaining open questions

After Round 1 + Round 2 the residue is small enough to take the
written defaults unless someone speaks up later.

### File-search UX (taking defaults)

- **OQ-F3.** Case-sensitive search toggle. **Default: no toggle (KISS).**
- **OQ-F4.** `ext:pdf` / `*.pdf` extension-as-syntax. **Default: no in v1.**
- **OQ-F5.** `PathSelector` popup shows files in Files / Both mode. **Default: no — popup stays folder-only.**
- **OQ-F6.** Tree view in empty-query state when mode is Files / Both. **Default: folders only — tree is for navigation, not results.**

### Bundles and symlinks (taking defaults)

- **OQ-F7.** `.app` bundles opaque. **Default: yes, don't descend.** (Already current behavior.)
- **OQ-F9.** Symlinks dedupe by canonical path. **Default: yes.**
- **OQ-F10.** macOS aliases. **Default: index alias file as-is, don't resolve.**

### File excludes (taking defaults)

- **OQ-F12.** File-cache cap exposure. **Default: in Preferences, not in the exclude dialog.**

### Performance (taking defaults)

- **OQ-F14.** Persistent on-disk cache snapshot for sub-second cold start. **Default: deferred to Phase D unless Phase B benches expose a need.**

### Content search — UX (taking defaults)

- **OQ-C1.** Auto-focus content field on threshold cross. **Default: no — deliberate user action.**
- **OQ-C3.** Multi-term content search semantics. **Default: AND (ripgrep `--all-match`), mirrors filename search.**
- **OQ-C5.** Per-file match limit. **Default: hardcode 20 v1, with "+M more" affordance.**

### Content search — engine (taking defaults)

- **OQ-C8.** Hard-skip extension list. **Default: approved as listed.**

### Content search — editor integration (taking defaults)

- **OQ-C10.** ⌥⏎ as the editor chord. **Default: free, goes into help line as `⌥⏎ editor`.**
- **OQ-C11.** Fallback when `code` not detected. **Default: fall through to plain `open file`; tooltip explains.**

### Cross-cutting (taking defaults)

- **OQ-X1.** Rename `PathCacheManager` → `FolderCacheManager`. **Default: no — keep upstream-derived name to minimize lift drift.**
- **OQ-X2.** QSettings INI alias for one release. **Default: yes.**
- **OQ-X3.** `ScanScheduler` changes. **Default: no — file cache piggybacks on existing scan walks.**
- **OQ-X5.** Telemetry. **Default: none, per `080_operations.md`.**
- **OQ-X6.** First-run hint on content-search activation. **Default: yes, one-time hint.**

### Risks worth flagging early

- **Memory ceiling.** 500k entries with avg 80-char paths = ~80 MB
  raw + ~120 MB Qt overhead = ~200 MB RSS just for file cache. Plus
  existing folder cache. Acceptable on modern Macs; flag for the
  performance budget doc.
- **Walk time.** Adding files to the BFS doubles per-directory work
  (one extra `entryList` call). Could push initial scan from ~30 s to
  ~50 s on Intel. Need to measure (Phase A `--bench` is the gate).
- **FSEvents storms.** A directory with many file mutations (e.g.
  `node_modules` install, even excluded) — the excluded-folder rule
  means we don't index, but the watcher still fires. Confirm
  `onDirectoryChanged` still does the right thing.
- **Code signing the bundled `rg`.** Required for Gatekeeper. Adds a
  step to the eventual notarization workflow tracked in
  `070_build_and_ship.md`.

---

## Test growth estimate

| Phase | New tests | Cumulative |
|---|---|---|
| A | ~15 | 208 |
| B | ~30 | 238 |
| C | ~25 | 263 |
| D | depends on what ships | — |

Existing 193 tests must stay green at every commit. Per the
contract in `CLAUDE.md`, `./br --test` gates every commit that
touches lifted code.

---

## Final plan — task breakdown

Each task is a single landable commit. `[ ]` = not started. Run
`./br --test` after every task. Bench numbers go into
`docs/030_performance_budget.md` whenever they change.

### Phase A — Foundations (~5 commits, ~15 tests)

Goal: nothing user-visible changes. Get the measurement + storage
layout right before any feature lands.

- **A1.** Split `ExcludeSettings` storage: `m_folderPatterns` + `m_filePatterns`. Keep the existing public API (`shouldExclude(folderName)`) untouched. Add `shouldExcludeFile(fileName)`.
- **A2.** QSettings INI migration: read legacy `[ExcludeSettings]/patterns` into `folderPatterns` if `folderPatterns` doesn't yet exist. Write both keys for one release. Test: load-from-legacy + load-from-new + load-from-both.
- **A3.** Default file-pattern list shipped (see decisions table).
- **A4.** `--bench` CLI flag. Emits JSON: scan-time per phase, search latency over 100 random queries (p50/p95/p99), folder count, file count (Phase B fills this in), memory RSS. Documented in `docs/100_dev_workflow.md`.
- **A5.** Initial bench run recorded in `docs/030_performance_budget.md` under a new "Measured bench results" section. Captures today's folder-only numbers as the baseline.

Gate to Phase B: `./br --test` green + bench JSON reproducible across two runs within 10 %.

### Phase B — File search v1 (~12 commits, ~30 tests)

- **B1.** New opaque-bundle skip list. Add to the path-level excludes alongside `/System` etc.: any path whose basename matches `*.app`, `*.photoslibrary`, `*.imovielibrary`, `*.musiclibrary`, `*.tvlibrary`, `*.aplibrary`. **Don't descend into these — applies to both folder and file scan.**
- **B2.** `FileCacheManager` singleton — same shape as `PathCacheManager`. `QStringList m_paths` + `QSet m_pathSet`, `QMutex m_mutex`. Public API: `cachedPaths()`, `search(query, rootPath, maxResults)`, `fileCount()`. **No subfolder-of-existing-match suppression** in `search()`.
- **B3.** Extend `PathCacheManager::scanWorker` to also enumerate `QDir::Files`, apply `m_excludeSettings->shouldExcludeFile()`, and call `FileCacheManager::instance()->addPathToCache()`. One scan walk, two destinations.
- **B4.** File-cache cap enforcement. Default 500 000. `Preferences` setting `fileCacheCap` (1k–10M range). When hit, scan stops adding files; signal `fileCapReached`.
- **B5.** Toolbar status surface: orange `File index cap reached (500 000)` text appears next to the existing folder-count badge when capped.
- **B6.** `FileSearchWorker` — mirrors `FolderSearchWorker`. Shares `fuzzyScore()` (already static, no change needed). Same 50 ms debounce.
- **B7.** Eye toggle symmetry: `FileSearchWorker::setIncludeHidden(bool)` + `pathIsHidden()` reuse from folder worker. Hidden files filtered post-cache like folders.
- **B8.** Segmented control widget in toolbar — `QButtonGroup` of three flat checkable buttons styled to look like the existing toolbar icons. `searchMode` persisted in QSettings, default `both`.
- **B9.** Results-list rendering: file glyph + dim extension chip on file rows; folder glyph unchanged. Existing purple highlighting reused.
- **B10.** Score-interleaved merge in `FolderBrowserDialog`: pull from both workers, merge by score, render. Tiebreaker = shorter path.
- **B11.** FSEvents extension: `onDirectoryChanged` also lists `QDir::Files` and diffs against the file-cache children.
- **B12.** `ExcludeSettingsDialog` two-tab layout (`Folders` | `Files`). Wires through `ExcludeSettings::addFilePattern` etc.
- **B13.** Dialog minimum width raised to **820** (from 720). Initial size bumped accordingly.
- **B14.** Run `--bench` against Benno's real `$HOME`. Record file-cache size, scan time, search latency p50/p95/p99 in `docs/030_performance_budget.md`. **This is the gate-defining measurement** — if p95 is above 200 ms, escalate to Phase D D1 (trigram index) before Phase C.

Tests added in B (~30):
- `FileCacheManagerTest` (~12): BFS populates, exclude patterns drop, FSEvents delete propagates, cap stops growth, hidden-file presentation, opaque-bundle skip.
- `FileSearchWorkerTest` (~6): basename match, multi-term AND, scoring, hidden filter, root scope.
- `ExcludeSettingsTest` (+6 in file branch): add/remove file pattern, glob semantics, default list, INI migration.
- `UserInteractionTest` (+6): segmented control toggles + persists, Files mode opens with app on ⏎, Both interleaves, eye applies to files, opaque bundles not in results, cap-status text appears under stress.

Gate to Phase C: tests green + bench numbers written down.

### Phase C — Content search v1 (~12 commits, ~25 tests)

- **C1.** Vendor `ripgrep` 14.x. Drop the macOS-arm64 binary at `third_party/rg/macos-arm64/rg`. Add `SHA256SUM` text file alongside. Add `README.md` in `third_party/rg/` documenting source URL + how to refresh.
- **C2.** CMake: copy `rg` to `<bundle>/Contents/Resources/rg` at install time. Add `codesign` step (ad-hoc signature OK for dev; real signing tracked in `070_build_and_ship.md`).
- **C3.** `RipgrepRunner` `QProcess` wrapper. Resolves binary path: `which rg` → bundled path → `nullptr`. Public API: `run(query, files, regex, callback)`; `cancel()`.
- **C4.** `ContentSearchSettings` (singleton, `QReadWriteLock`): `threshold` (default 1000, range 100–5000), `maxFileSizeMB` (default 5), `extBlacklist` (defaults from decisions table). Persisted in QSettings.
- **C5.** Second `QLineEdit` "Inside contents:" placed under the filename query. State-machine gating implemented in `FolderBrowserDialog`. Hint text reflects the four states.
- **C6.** `Regex` checkbox next to the content field. Default OFF. When OFF, the query is passed to ripgrep with `-F` (fixed-string). When ON, regex mode (default ripgrep behavior).
- **C7.** `?` help popover next to the regex toggle. **10 examples**: case-insensitive `(?i)foo`, word boundary `\bfoo\b`, alternation `foo|bar`, line anchor `^TODO`, end anchor `;\s*$`, char class `[A-Z]{3,}`, quantifier `\d{3,}`, negation `[^/]+`, escaped dot `1\.2\.3`, capture group `id=(\d+)`. Shown only when regex mode is on; hidden when off.
- **C8.** Result-list switches to expandable rows in content mode. Top row = file + match count; expand → child rows `Line N: snippet`. Snippet truncated to 200 chars. Per-file cap 20 matches with "+M more". Existing purple highlighting reused on the matched span.
- **C9.** Streaming parser: read ripgrep `--json` line by line, deserialize, append rows incrementally. Cancel-on-keystroke kills the in-flight `QProcess`.
- **C10.** Will-open footer: when a content-line child row is selected, footer text is `Will open: /abs/path/file.cpp:42`. When parent file row is selected, footer is the file path only.
- **C11.** VS Code detector. Order: `which code` → `/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code`. Cached on first detection. ⌥⏎ chord wired to `code --goto "{file}:{line}:{col}"`. Fallback when not detected: plain `open file`. Tooltip on the help line.
- **C12.** First-run one-time hint when the content field first activates: small toast or inline label, `New: type here to search inside the matching files. Press ⌥⏎ to open at the matched line in VS Code.` Dismissed by clicking or by typing in the field.
- **C13.** Help line update: append `⌥⏎ editor` to the existing chord summary.

Tests added in C (~25):
- `RipgrepRunnerTest` (~10): spawn + parse JSON, cancel, missing binary, oversize file skip, binary file skip, multi-term `--all-match`, fixed vs regex modes.
- `ContentSearchSettingsTest` (~6): threshold range, default values, persistence, extension blacklist, file-size cap, defaults round-trip.
- `UsabilityTest` extensions (~9): content field state at all four boundaries (0, 1, threshold, threshold+1), expand/collapse via keyboard, ⌥⏎ behavior with and without `code`, Will-open footer reflects line, regex help popover shows 10 examples, first-run hint shown once.

Gate to release: all 263 tests green + ripgrep signed + `--bench` re-run captures content-search latency numbers.

### Phase D — Polish (deferred unless Phase B/C demands)

- **D1.** Trigram index on lowercased basenames in `FileCacheManager`. Trigger condition: Phase B bench shows p95 > 200 ms or user report.
- **D2.** PDF / DOCX / XLSX content extraction. Separate opt-in Preferences toggle `Also search inside documents (PDF, Word, Excel) — slower`. Implementation: `pdftotext` (poppler) bundled, plus an internal unzip-and-extract-xml-text path for office formats.
- **D3.** Persistent on-disk cache snapshot. Saves `$HOME/.macos-search/cache.bin` on shutdown; loads on startup before scan. Scan still runs to verify but UI is usable instantly.

---

## Risks tracked

- **Memory.** ~200 MB RSS for the file cache alone at the 500k cap. Acceptable on modern Macs; surface in `docs/030_performance_budget.md` after Phase B14.
- **Walk time.** Adding `QDir::Files` per directory roughly doubles per-directory enumeration cost. Phase B14 measures real impact.
- **FSEvents storms.** Confirm `onDirectoryChanged` does the right thing under load when a directory mutates rapidly (e.g. extracting a large archive into Downloads).
- **Code signing the bundled `rg`.** Ad-hoc signature OK for dev; proper Developer ID signing needed for eventual notarization, tracked in `070_build_and_ship.md`.
- **Vendor binary drift.** ripgrep checked into git means updates are deliberate. Document refresh procedure in `third_party/rg/README.md`.
- **Cross-thread access.** Every new cache, settings class, and worker follows the `ExcludeSettings` `QReadWriteLock` pattern. No exceptions. Re-read `120_qt_threading.md` before each phase.
