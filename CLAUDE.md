# CLAUDE.md

Guidance for Claude Code working in this repository.

## Status

Working app. The cache + search subsystem is lifted from `../maude-cp-v3`,
the standalone-app shell (favorites sidebar, two open buttons, dialog
framing, keyboard map) is local. **101 tests across 6 classes, all
passing**. See `docs/000_index.md` for the full doc set; deltas vs.
upstream live in `docs/050_porting_rules.md`.

## Where things are documented — read these BEFORE changing code

| If you're touching… | Read first |
|---|---|
| `ExcludeSettings`, `PathCacheManager`, `FolderSearchWorker` (anything threaded) | `docs/120_qt_threading.md` |
| `FolderBrowserDialog` keyboard / mouse handling | `docs/140_keyboard_shortcuts.md`, `docs/020_ux_contract.md` |
| The favorites sidebar | `docs/130_favorites.md` |
| Cache or search algorithm | `docs/qt-reference/050_folder_search.md` (verbatim upstream) |
| `PathSelector/*` | `docs/qt-reference/051_path_selector.md` |
| Anything Qt — memory or crash class issues | `docs/qt-reference/041_memory_management.md`, `docs/qt-reference/042_crash_prevention.md` |
| Tests / harness | `docs/060_test_strategy.md` |
| Dev workflow / screenshot loop / macOS permissions | `docs/100_dev_workflow.md` |
| Lift history / what came from where | `docs/050_porting_rules.md` |

The full doc index is `docs/000_index.md`. **This is a standalone fork
of `../maude-cp-v3`.** No backport obligation; change anything that
makes the app better.

## What this project is

A standalone macOS app that lifts the cached-search + path-selector
subsystem out of `../maude-cp-v3` and wraps it in a dedicated dialog —
sidebar of favorite roots, two "open" actions (in Finder / with default
app), full mouseless keyboard map. UX target per the README: feel like
Finder's ⌘⇧G but much better, designed for non-technical end users.

## Hard rules

1. **Qt threading.** A `QObject` belongs to one thread. Non-thread-safe
   state needs a lock for cross-thread access. The cache scan workers
   run on N background QThreads; anything they touch from the main
   thread needs `QReadWriteLock` / `QMutex` protection. Full pattern in
   `docs/120_qt_threading.md`. Don't add cross-thread access without
   reading it.

2. **No backport thinking.** This is an independent fork. The lifted
   files (`PathCacheManager`, `FolderSearchWorker`, `SearchField`,
   `ExcludeSettings`, `PathSelector/*`, support classes) keep their
   upstream-derived structure where that still makes sense, but you
   are free to refactor, add API surface, or fix bugs without
   considering whether the change is mergeable back to
   `../maude-cp-v3`. Record substantive local changes in
   `docs/050_porting_rules.md` under "Local extensions" so future
   contributors know what's been touched.

3. **Tests gate releases.** `./br --test` must pass before any commit
   that touches the lifted code. 101 tests, ~5 s wall-clock. The
   `UserInteractionTest` class regression-locks the keyboard-driven
   UX — typing-appends-not-replaces, Esc-doesn't-close-dialog, etc.

4. **Never use blind `osascript ... keystroke ...` in scripts.** The
   `ui-drive.sh quit` regression once nuked unrelated VS Code windows
   because the fallback Cmd-Q targeted whatever was frontmost. All
   System Events keystroke commands now `require_running` the named
   target and refuse otherwise. See `docs/100_dev_workflow.md`.

5. **macOS-only.** No Linux / Windows branches. Lifted code's
   inotify / `ReadDirectoryChangesW` paths are stripped on import.

## Stack

**C++17 / Qt 6 (Homebrew) / CMake ≥ 3.21.** Apple Clang. macOS only.
Decided — do not propose Swift/SwiftUI alternatives.

## Build pattern

- `./br` → debug build + run (default `build-benno/`).
- `./br --who=claude` → use `build/` (automation / CI).
- `./br --test` → run the test suite.
- `./br --detach` → launch in background; return; for screenshots.
- `./br -c` → clean rebuild.
- `./br --help` → full flag set.

Build dirs are isolated by `--who=` so human and automation don't
collide. Tests run with `QT_QPA_PLATFORM=offscreen` by default; override
with `QT_QPA_PLATFORM= ./build/macos-search_tests` to see them.

## Source layout

```
src/
  PathCacheManager.*       (lifted)  in-memory folder cache + BFS scan
  FolderSearchWorker.*     (lifted)  debounced multi-term search
  SearchField.*            (lifted)  debounced QLineEdit wrapper
  ExcludeSettings.*        (lifted, +QReadWriteLock for cross-thread safety)
  ExcludeSettingsDialog.*  (lifted)  pattern editor
  PathSelector/            (lifted)  5-state path-completion widget family
  SwiftUIStyle.*           (lifted)  palette + stylesheet
  IconRegistry.*           (lifted)  SVG icon registry + tinting
  ThemeManager.*           (lifted)  dark/light detection
  MaudeConfig.*            (lifted, retargets ~/.maude → ~/.macos-search)
  FolderBrowserDialog.*    (lifted + significant standalone-app drift)
  main.cpp                 (local)   entry point

tests/                     6 QTest classes + aggregate runner
assets/icons/              SVGs registered via icons.qrc
docs/                      see docs/000_index.md
scripts/                   br.sh, screenshot.sh, ui-drive.sh
```

## Proposed features tracked but NOT built

`docs/110_features_autostart_and_hotkey.md`:

- Autostart (LaunchAgent or `SMAppService`) so the cache warms before
  the user types.
- Global hotkey **⌃⌥⇧S** to summon (Carbon `RegisterEventHotKey`).

## What changed in this iteration's session (May 18, 2026)

(For Claude resuming context after compaction — high-signal changes:)

1. Lifted the entire `FolderBrowserDialog` family, all dependencies,
   and 6 SVG icons. App is now the upstream picker as its main window.
2. Replaced single "Choose" with **Open in Finder** + **Open with App**.
   Dialog stays open after open (it's the running app, not a modal).
3. Added the **Finder-style favorites sidebar** with `Home, Documents,
   Downloads, Desktop, Macintosh HD` seeded on first run. Right-click
   → *Make default* / *Delete* (Delete hidden for Home).
4. Wrote **24-test `UserInteractionTest`** simulating user keystrokes
   and clicks against a live dialog. Regression-locked: typing appends
   (was: replaced), Esc doesn't close the dialog (was: did), ↑/↓/PgUp/
   PgDn forward from search field to visible view, ⌘L/⌘F/⌘H/⌘↑/⌘⏎/⏎
   all do what they say.
5. **Fixed the real threading bug** in `ExcludeSettings` — added
   `QReadWriteLock`, removed the `aboutToQuit`-stopScan workaround
   that was papering over it. Full write-up in `docs/120_qt_threading.md`.
6. Persistent shortcuts hint line at the bottom of the dialog. Favorites
   rendered as cards (1 px border, soft bg, hover, selected states);
   default is bold (no bubble prefix).
7. Hardened `ui-drive.sh quit` so it never targets a wrong app.

## Notes that don't fit elsewhere

- `claude.exe` is in the process chain when Claude Code runs Bash tool
  calls (`VS Code → Code Helper → zsh → claude → zsh → osascript`).
  macOS TCC attributes Accessibility to the responsible **GUI parent**
  (VS Code), not to the CLI binary. The "Claude" entry in the
  Accessibility list is the Claude Desktop app — not this CLI. To grant
  the test harness keystroke synthesis, grant Accessibility to your
  terminal-hosting app. Diagnostic: `/usr/bin/log show --last 1m
  --style compact | grep AUTHREQ_ATTRIBUTION`.

- The `aboutToQuit` `stopScan()` connection in `main.cpp` is now
  *polite teardown*, not crash prevention — keep it, but the
  `QReadWriteLock` in `ExcludeSettings` is what actually makes the
  shutdown safe.

- For interactive UI testing without writing C++:

  ```sh
  ./br --detach
  ./scripts/screenshot.sh some-label
  ./scripts/ui-drive.sh type "trafo"
  ./scripts/screenshot.sh trafo
  ./scripts/ui-drive.sh quit
  ```

  Output is in `screenshots/`. Keystroke synthesis needs macOS
  Accessibility for the parent terminal.

## Release & operations (added 2026-07-23)

- **Deploy:** `scripts/deploy.sh` builds Release, embeds Qt, signs with the
  STABLE self-signed identity **"macos-search Benno Loeffler"** (so macOS TCC
  folder grants persist across rebuilds — ad-hoc signing re-prompts every
  build), installs to `/Applications`, and launches. `--no-launch`,
  `--reset-tcc` flags. One-time cert setup is documented at the top of the
  script.
- **Dropbox release location** (internal V&S distribution):
  `VundS Dropbox/VundS/B_Org Shop/B_05_IT/B_525_KI/macos-search-releases/` —
  one `macos-search-<version>[-date]/` folder per build with `macos-search.app`,
  a `.zip`, and a `README.md` install guide (right-click → Open on first run;
  allow Desktop/Downloads/Documents once).
- **Self-reflection health log:** the app runs `HealthMonitor` (a main-thread
  heartbeat + an independent background logger) writing `~/.macos-search/health.log`
  every second: memory, thread count, scan state, counts, and main-thread stall
  status. On a main-thread stall (>3 s) it auto-dumps a `sample` of the frozen
  stack to `~/.macos-search/health-stall-*.txt`. **To debug a freeze, read those
  files** — a stalled `main=STALLED` line + the stall sample pinpoint the hang
  (that's how the FSEvents `indexNewSubtree` main-thread hang was found).
