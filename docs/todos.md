# TODOs

Living list of work. Items below are organised as **Done**, **Open**,
and **Future / nice-to-have**. Anything completed has the
corresponding tests + source pointers so you can find the
implementation quickly.

---

## Test scoreboard

```
ExcludeSettingsTest     31
PathCacheManagerTest    13
SearchFieldTest         16
PathSelectorStateTest    8
FolderBrowserDialogTest  9
UserInteractionTest     25
UsabilityTest           44
CacheStrategyTest        9
──────────────────────  ───
                       155
```

All green. Run with `./br --test`.

---

## ✅ Done

### TODO 1 — Bulletproof every user interaction

Implemented 44 tests in `tests/UsabilityTest.{h,cpp}`. Tests live
under `UsabilityTest::*` and follow stable `T-XXX` IDs from the
catalog (see TODO 2).

**Categories covered:**

- Focus & traversal — initial focus, ⌘F/⌘L focus targets, Esc-keeps-focus.
- Global chords — ⌘F, ⌘L, ⌘⇧G, ⌘H, ⌘↑, ⌘⏎, ⏎, Esc, ⌘Q no-crash.
- Typing — multi-word, ⌘F-then-replace, slash literal.
- Cursor — Home/End in search field not intercepted.
- Mouse — up/home buttons, eye toggle, tree click, both Open buttons.
- Favorites — Make default persists, Delete persists, deleting default
  falls back to Home.
- View-stack — empty query → tree, non-empty → results, clear returns,
  Will-open reflects selection.
- Suppression — repeated Esc never closes the dialog.
- Discoverability — every toolbar button has a tooltip; search field
  has placeholder + clear button; default favorite is bold (no bubble).
- Cross-action consistency — resolveDefault matches setDefault,
  delete-then-fallback, settings propagate across dialog instances,
  sidebar always has Home.
- Performance smoke — dialog ctor < 1500 ms, 100 keystrokes < 3 s.

Plus 24 regression-locks in `UserInteractionTest` from earlier:
typing-appends-not-replaces, Esc-doesn't-close, arrow-forwarding etc.

### TODO 2 — Usability test catalog

`docs/150_usability_tests.md` — 80-row catalog with stable IDs,
grouped into 10 categories. Tests in `UsabilityTest` reference the
catalog IDs in slot-name comments (`// T-067b`).

### TODO 3 — Cache strategy: priority queue driven by favorites

Implemented in `src/main.cpp` + `src/PathCacheManager.cpp`.

- New `ScanScheduler` (anonymous-namespace QObject in `main.cpp`)
  reads the favorites from `QSettings`, prepends the default,
  filters for existence, dedups. Scans the first via `startScan()`;
  on each `scanComplete` signal, chains the next via `expandTo()`.
- Path-level excludes baked into `PathCacheManager::scanWorker`:
  `/System`, `/private`, `/dev`, `/Volumes`, `/cores`,
  `/.fseventsd`, `/.Spotlight-V100`, `/.DocumentRevisions-V100`,
  `/.PKInstallSandboxManager*`, `/.Trashes`, `/.TemporaryItems`,
  `/.MobileBackups`, `/.HFS+ Private Directory Data`.
- Excluded entries increment `m_foldersExcluded` so the status line
  still tracks them.

Tests: 9 in `tests/CacheStrategyTest.{h,cpp}` — `/System`, `/private`,
`/dev`, `/Volumes/*` never reach the cache; `/Users` DOES (sanity
that the excludes aren't over-broad); `expandTo` handles multiple
roots and dedups already-covered paths.

### TODO 4 — "Show hidden" must not re-index

Decoupled hidden-folders from the cache.

- `PathCacheManager::setShowHidden` is now a no-op. The scan workers
  always use `QDir::Hidden`.
- New `FolderSearchWorker::setIncludeHidden(bool)` +
  `FolderSearchWorker::pathIsHidden(path)` static helper that splits
  the path by `/` and returns true if any segment starts with `.`.
- `FolderBrowserDialog::onShowHiddenToggled` updates only
  presentation: the `QFileSystemModel` filter (tree view), the
  `PathSelector` adapter (completion popup), and the search worker.
  Re-runs the current query so visible results reflect the new
  state immediately. **No rescan triggered.**

Tests: 2 new entries in `UsabilityTest` —
`eyeToggleDoesNotRescan` (T-067b) and
`eyeToggleHidesHiddenSearchResults` (T-067c).

---

## 📋 Open

### TODO 5 — First-run autostart prompt

On the very first launch (no `firstRunCompleted` key in QSettings),
show a one-shot modal:

> **Start macos-search automatically when you log in?**
> Keeping the app running in the background means the folder index is
> already built when you need to search — no waiting.
>
> _You can change this anytime in Preferences._
>
>   [Skip]   [Yes, enable autostart]

Decisions:

- Default focus on **[Yes, enable autostart]** so Enter accepts.
- Persist the choice and never ask again — set
  `QSettings("Maude", "FolderBrowser")` → `firstRunCompleted=true`
  regardless of choice.
- If the user picks **Yes**, also persist `autostart=true` and
  register the app with the OS at-login launcher.
- If **Skip**, leave `autostart=false` — user can flip it later via
  a Preferences menu item (TODO 7, future).

Implementation notes (see `docs/110_features_autostart_and_hotkey.md`
for the API choice — `SMAppService` on macOS 13+, fall back to
LaunchAgent plist on older releases):

```cpp
// src/Autostart.{h,cpp}  (new)
namespace Autostart {
    bool isEnabled();
    void setEnabled(bool);
}
```

Surface in `main.cpp`:

```cpp
QSettings s("Maude", "FolderBrowser");
if (!s.value("firstRunCompleted", false).toBool()) {
    showFirstRunDialog();  // sets firstRunCompleted=true on close
}
```

**Tests** (`UsabilityTest`):

- `firstRunPromptShowsOnFreshSettings` — fresh QSettings → modal exists.
- `firstRunPromptYesPersistsAutostart` — accepting sets `autostart=true`
  + `firstRunCompleted=true`.
- `firstRunPromptSkipPersistsCompletedOnly` — skip sets
  `firstRunCompleted=true` only.
- `firstRunPromptShowsOnceOnly` — second launch with
  `firstRunCompleted=true` does NOT re-show.

### TODO 6 — Global hotkey ⌃⌥⇧S

Register **Control + Option + Shift + S** (mnemonic: **S**earch)
system-wide so the user can summon the app from any frontmost
context. Pressing it must:

- If the app is hidden / minimized / behind: show, raise, activate.
- Focus the search field and `selectAll()` so the next keystroke
  replaces the previous query.
- If the app is already focused: re-focus + select-all (idempotent).

**API choice**: Carbon `RegisterEventHotKey`. Despite the name it is
NOT deprecated and does NOT require macOS Accessibility permission
(unlike `NSEvent addGlobalMonitorForEventsMatchingMask:`). Used by
Alfred, Raycast, Sublime, MacVim, etc.

Sketch:

```cpp
// src/GlobalHotkey.{h,cpp}  (new, links -framework Carbon)
#include <Carbon/Carbon.h>

class GlobalHotkey : public QObject {
    Q_OBJECT
public:
    void registerSummonChord();   // ⌃⌥⇧S
signals:
    void summonRequested();
private:
    EventHotKeyRef m_ref = nullptr;
};
```

Wire into `main.cpp`:

```cpp
auto *hotkey = new GlobalHotkey(&app);
hotkey->registerSummonChord();
QObject::connect(hotkey, &GlobalHotkey::summonRequested, &dialog, [&] {
    dialog.show(); dialog.raise(); dialog.activateWindow();
    if (auto *f = dialog.findChild<QLineEdit*>("searchField")) {
        f->setFocus(); f->selectAll();
    }
});
```

**Conflict handling**: if `RegisterEventHotKey` returns
`eventHotKeyExistsErr` (another app already grabbed ⌃⌥⇧S), show a
one-time toast/snackbar (`"Could not register ⌃⌥⇧S — another app is
using it"`) and continue running without the chord. Don't block
startup.

**Update the persistent hint line** at the bottom of the dialog to
mention the summon chord — e.g.
`⌃⌥⇧S summon · ↑↓ nav · ↵ open · …`. The line is rendered in
`FolderBrowserDialog::setupUi` near the `shortcutsHint` widget.

**Tests** (`UsabilityTest`):

- `globalHotkeyRegistersWithoutCrash` — call `registerSummonChord`,
  no crash, `m_ref != nullptr`. Skipped if `RegisterEventHotKey`
  isn't available in the test environment.
- `summonSignalShowsAndFocusesSearch` — emit `summonRequested`
  manually (don't actually press the system chord); assert
  `dialog.isVisible() && searchField->hasFocus() && searchField->selectedText() == previousText`.
- `summonClipsAreReflectedInHintLine` — `shortcutsHint->text()`
  contains `⌃⌥⇧S` once the hotkey is wired.

### TODO 7 — Preferences menu item (depends on TODOs 5 + 6)

Once autostart + hotkey are persisted in QSettings, add a Preferences
modal (gear icon already exists in the toolbar) to flip them at any
time without re-launching:

- `[x] Start macos-search automatically at login`
- `[x] Enable global hotkey ⌃⌥⇧S to summon the app`
- `[ ] Show hidden folders` (already wired to the eye toggle —
  duplicated here for discoverability)

Out of scope until TODOs 5 + 6 are in.

---

## 🌱 Future / nice-to-have

### Cache strategy — Phase 2

- **Scope pill in toolbar**: `Indexed: ~ · Documents · /Applications`.
  Clicking opens the favorites sidebar editor. Lets the user see
  cache coverage at a glance.
- **"Index complete disk"** one-shot button (separate from favorites)
  with a confirm dialog showing estimated time. For power users who
  want full-disk search without adding `/` as a permanent favorite.

### UI polish

- **Match highlighting in the tree view**: currently only the
  search-results list highlights matched fragments in purple. The
  tree view shows folder names unstyled.
- **Drag a folder to add as favorite**: drop-target on the sidebar.
- **Quick Look (Space)**: preview the selected file/folder without
  opening it.
- **Reveal-in-results glyphs on row hover**: small Finder + App
  icons per row, in addition to the bottom buttons.

### Discoverability

- **First-run intro pane** — one short modal explaining the
  favorites sidebar, the keyboard map, and "I'll index your home
  folder, nothing leaves this Mac" (privacy story).

### Power-user (now in **📋 Open** above)

- Global hotkey ⌃⌥⇧S to summon — moved to TODO 6.
- Autostart at login — moved to TODO 5.

### Build & ship

- **Universal binary** (currently arm64-only).
- **Notarized DMG** with code signing — required for public
  distribution.
- **CI**: GitHub Actions running `./br --test` on macOS-latest.

### Tests

- **Right-click context menu E2E**: today the menu's data effects
  are tested by calling `setDefaultFavorite` / `removeFavorite`
  directly. To test the menu itself end-to-end without hanging on
  `QMenu::exec()`, either extract the menu builder into a
  pure-function helper returning `QList<QAction*>`, or use
  `QTimer::singleShot` to dismiss the menu during the test.
- **Tab-traversal test** through every focusable widget — currently
  individual focus targets are tested but not the full forward/back
  traversal order.

### Drift / lift cleanup

- The `MainWindow.{cpp,h}` + `SearchResultModel.{cpp,h}` files
  exist from an earlier iteration but are unused — the app's
  top-level is `FolderBrowserDialog` directly. They could be deleted
  if we're sure no future feature needs them.

---

## Decisions already made (no longer open)

- macOS only. No Linux/Windows branches.
- This is a **standalone fork** of `../maude-cp-v3`. No backport
  obligation.
- Default scan strategy is favorites-driven; `/` is just another
  favorite with seeded path-level excludes.
- Eye toggle is presentational only.
- Home is the implicit default; `defaultFavorite=""` in `QSettings`
  means "use Home".
- The persistent keyboard hint line lives at the bottom of the
  dialog and is the discoverability path for chords.
