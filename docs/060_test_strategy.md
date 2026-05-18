# Test Strategy

What's built today.

## Test runner

Single aggregate binary: `macos-search_tests`. Run with `./br --test`.

- Source: `tests/test_main.cpp`.
- Each test class is its own `QObject` with QTest `private slots`.
- `test_main` `qExec`s them in sequence and ORs the result codes.
- Filter to one class: `./build/macos-search_tests --filter PathSelectorStateTest`.
- Headless: runs with `QT_QPA_PLATFORM=offscreen` (set automatically
  unless the env var is already set). To see windows visually run with
  `QT_QPA_PLATFORM= ./build/macos-search_tests`.

## Layers

### Layer 1 — Ported unit tests (from upstream)

| Class | Tests | Source |
|---|---|---|
| `ExcludeSettingsTest` | 31 | Verbatim except `MaudeConfig` removal. |
| `PathCacheManagerTest` | 13 | Verbatim. |
| `SearchFieldTest` | 16 | Verbatim minus the `testUsesSwiftUIStyleSheet` SwiftUIStyle-dependency check. |

These nail down the upstream-derived contract: state-machine
correctness, debounce timing, default exclude patterns, etc.

### Layer 2 — Locally written model tests

| Class | Tests | Focus |
|---|---|---|
| `PathSelectorStateTest` | 8 | The 5-state machine driven against a real `QTemporaryDir`. |
| `FolderBrowserDialogTest` | 9 | Construction, both Open buttons exist, favorites persistence, default-favorite, non-existent-path filtering, first-run seeded defaults. |

These cover the lifted Qt code at the model level — what changed when
the data changes.

### Layer 3 — User-perspective integration tests (new)

`UserInteractionTest` — **24 tests** simulating real keystrokes and
mouse clicks via `QTest::keyClick` / `QTest::mouseClick` against a
live `FolderBrowserDialog`. Coverage:

- **Typing**: appending works (regression-locks the old "type replaces"
  bug); typing while focus is in the tree-view still lands in the
  search field; modifier chords don't get treated as printable input.
- **Navigation**: ↑/↓/PgUp/PgDn from the search field forwards to the
  visible view; favorites list keeps its own arrows.
- **Chords**: ⌘L → path field focus, ⌘F → search field, ⌘H → home,
  ⌘↑ → parent, ⏎ → Open with App, ⌘⏎ → Open in Finder.
- **Esc safety**: clears non-empty search; on empty search does NOT
  close the dialog (regression-locked — used to fall through to
  `QDialog::reject`).
- **Favorites**: clicking switches root; `+ Add current` persists;
  Home is ignored as duplicate; same path can't be added twice.
- **Open buttons** populate `selectedPath()`.
- **Help line** is present and mentions ⌘ and Esc.

## Total

**101 tests across 6 classes, ~5 s wall-clock**, all passing.

```
ExcludeSettingsTest     31
PathCacheManagerTest    13
SearchFieldTest         16
PathSelectorStateTest    8
FolderBrowserDialogTest  9
UserInteractionTest     24
────────────────────── ───
                       101
```

## Conventions and learnings

### Tests share an `ExcludeSettings`-bearing `PathCacheManager`

The cache is a global singleton. `FolderBrowserDialogTest::initTestCase`
and `UserInteractionTest::initTestCase` give it a static
`ExcludeSettings` instance so its background workers (which DO get
spawned by `FolderBrowserDialog::loadSettings` → `expandTo`) never
dereference a null `m_excludeSettings`.

### Tests reset persisted state per test

`init()` clears `QSettings("Maude", "FolderBrowser")` keys and calls
`PathCacheManager::stopScan()` so the previous test's scan threads
don't race the next test's setup.

### Tests drive keystrokes through `QApplication::focusWidget()`

`QTest::keyClick(&dialog, ...)` always delivers to the dialog,
bypassing the focus chain. Real users' keystrokes route through the OS
to the focused widget. The test helper `typeString()` mirrors this.

### Tests target `QListWidget` for arrow-forwarding, not the tree view

`QFileSystemModel` has its own background `QFileInfoGatherer` thread.
On the offscreen platform, that thread can race teardown when many
dialogs are created in sequence — `SIGSEGV`. Arrow-key forwarding
tests use the deterministic search-results `QListWidget` instead.

### Tests do NOT use `osascript`

`osascript` keyboard synthesis would need macOS Accessibility permission
for the responsible process (`claude.exe` CLI or VS Code). All test
keystrokes are in-process via `QTest::keyClick`, so no permission is
required.

### Tests do NOT exercise `QMenu::exec()`

`QMenu::exec()` is modal and blocking — would hang the test. Right-click
menu behavior is tested by calling the underlying state mutators
(`setDefaultFavorite`, `removeFavorite`) directly. The menu wiring
itself is best verified manually via the screenshot workflow.

See `120_qt_threading.md` for the threading background that drove
these decisions.

## Manual UAT loop

The screenshot workflow in `100_dev_workflow.md` covers what unit tests
can't (visual polish, real-Finder integration of `open -R`, animation
smoothness). Run after any UI change:

```
./br --detach
./scripts/screenshot.sh some-label
./scripts/ui-drive.sh quit
```

## CI

Not set up. `./br --test` exit code is the local gate. Adding GitHub
Actions running the test binary on macOS is tracked in
`090_open_questions.md`.
