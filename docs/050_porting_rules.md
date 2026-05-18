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
| `src/PathCacheManager.cpp` | Path-level system excludes (`/System`, `/private`, `/dev`, `/Volumes`, …) so `/` can be scanned without bloating the cache with system files. `setShowHidden` is now a no-op — the cache always indexes hidden folders; the eye toggle is presentational only. See `docs/todos.md` TODO 4. |
| `src/FolderSearchWorker.{h,cpp}` | Added `setIncludeHidden(bool)` + static `pathIsHidden(path)`. Filters cache results that contain hidden path segments. |
| `assets/icons.qrc` | Minimal subset of upstream's icon resource set. |
| `assets/macos-search.icns` | New app icon. |

## License headers

Same as upstream `maude-cp-v3` LICENSE for the lifted modules, until
the distribution path is decided. Local extensions inherit the same.
