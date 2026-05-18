# TODOs

Living list of work, verified item-by-item against the actual source
and test run (not against the prose claims). Checkboxes are at the
level of individual, testable pieces:

- `[x]` — checked against code or a passing test on 2026-05-18.
- `[ ]` — not implemented, or claim could not be verified.

---

## Test scoreboard

Re-counted from `./br --test` on 2026-05-18:

```
ExcludeSettingsTest     31   [x]
PathCacheManagerTest    13   [x]
SearchFieldTest         16   [x]
PathSelectorStateTest    8   [x]
FolderBrowserDialogTest  9   [x]
UserInteractionTest     25   [x]
UsabilityTest           47   [x]   (+3 dialog-stays-open regression-locks)
CacheStrategyTest        9   [x]
AutostartTest           15   [x]   (TODO 5)
GlobalHotkeyTest         8   [x]   (TODO 6)
PreferencesDialogTest   12   [x]   (TODO 7)
──────────────────────  ───
                       193
```

- [x] All 193 tests green via `./br --test`.

---

## TODO 1 — Bulletproof every user interaction

44 tests in `tests/UsabilityTest.{h,cpp}`. Verified each slot exists
and passes.

**Focus & traversal**

- [x] `initialFocusIsSearchField` — initial focus lands on search field.
- [x] `cmdFAlwaysLandsOnSearchField` — ⌘F focuses search.
- [x] `cmdLAlwaysLandsOnPathField` — ⌘L focuses path.
- [x] `escClearKeepsFocusOnSearchField` — Esc keeps focus on search.

**Global chords**

- [x] `cmdF_focusesAndSelectsAll` — ⌘F focuses + selectAll.
- [x] `cmdShiftG_focusesPathField` — ⌘⇧G focuses path.
- [x] `cmdUp_atRootIsNoOp` — ⌘↑ at root no-ops.
- [x] `escEmptyDoesNotCloseDialog` — Esc on empty doesn't close.
- [x] `cmdQ_doesNotCrash` — ⌘Q no crash.
- [x] ⌘H jump to Home (covered via `homeButtonJumpsHome` + `cmdHJumpsToHome` in UserInteractionTest).
- [x] ⌘⏎ open-in-Finder, ⏎ open-with-app (covered in UserInteractionTest:
  `enterTriggersOpenWithAppAction`, `cmdEnterTriggersOpenInFinderAction`).

**Typing**

- [x] `multiWordQueryTyped` — multi-word query typed correctly.
- [x] `cmdFThenTypingReplacesExisting` — ⌘F then typing replaces.
- [x] `slashTypedInSearchAppendsLiterally` — `/` is literal in search.

**Cursor**

- [x] `homeEndInSearchFieldNotIntercepted` — Home/End not intercepted.

**Mouse**

- [x] `upButtonGoesToParent` — Up button → parent.
- [x] `homeButtonJumpsHome` — Home button → Home.
- [x] `eyeToggleFlipsAndPersists` — Eye toggles + persists.
- [x] `singleClickTreeRowSetsScope` — Tree click sets scope.
- [x] Both Open buttons fire correct actions (covered in
  UserInteractionTest: `openInFinderButtonInvokesReveal`,
  `openInAppButtonInvokesOpen`).

**Favorites**

- [x] `makeDefaultPersists` — Make default persists.
- [x] `deleteRowPersists` — Delete persists.
- [x] `deletingDefaultFallsBackToHome` — Deleting default → Home fallback.

**View-stack**

- [x] `emptyQueryShowsTree` — empty → tree.
- [x] `nonEmptyQueryShowsResults` — non-empty → results.
- [x] `clearQueryReturnsToTree` — clear returns to tree.
- [x] `willOpenReflectsSelection` — Will-open reflects selection.

**Suppression**

- [x] `repeatedEscNeverCloses` — repeated Esc never closes.

**Discoverability**

- [x] `upButtonHasTooltip` — Up button tooltip.
- [x] `homeButtonHasTooltip` — Home button tooltip.
- [x] `eyeButtonHasTooltip` — Eye button tooltip.
- [x] `gearButtonHasTooltip` — Gear button tooltip.
- [x] `searchFieldHasPlaceholder` — search field placeholder.
- [x] `searchFieldHasClearButton` — search field clear button.
- [x] `defaultFavoriteIsBoldNotBubbled` — default favorite bold, no bubble.
- [x] `nonDefaultFavoritesAreNotBold` — non-defaults not bold.

**Cross-action consistency**

- [x] `resolveDefaultMatchesSetDefault` — resolveDefault matches setDefault.
- [x] `deletingDefaultMakesHomeDefault` — delete-then-fallback.
- [x] `addThenDefaultThenDeleteIsConsistent` — add/default/delete consistent.
- [x] `favoritesPropagateAcrossDialogInstances` — settings cross instances.
- [x] `sidebarAlwaysHasAtLeastHome` — sidebar always has Home.

**Performance smoke**

- [x] `dialogConstructsQuickly` — ctor < 1500 ms.
- [x] `rapidKeystrokesDoNotBlock` — 100 keystrokes < 3 s.

**Regression-locks in `UserInteractionTest`**

- [x] 25 regression slots all green (file documented as 24; actual = 25).

---

## TODO 2 — Usability test catalog

- [x] `docs/150_usability_tests.md` exists (189 lines).
- [x] 80 rows with stable `T-XXX` IDs (`grep -c "^| T-"` = 80).
- [x] Grouped into categories (10 sections in the doc).
- [x] Tests reference catalog IDs in slot-name comments (e.g. `// T-067b`
  in `UsabilityTest.cpp`).

---

## TODO 3 — Cache strategy: priority queue driven by favorites

- [x] `ScanScheduler` class exists (`src/main.cpp:47`).
- [x] Reads favorites from `QSettings` and assembles queue in `main.cpp`
  before instantiating `ScanScheduler`.
- [x] Chains scans on `PathCacheManager::scanComplete` →
  `ScanScheduler::onScanComplete` (`src/main.cpp:54-55`).
- [x] Calls `m_cache->expandTo(next)` to chain (`src/main.cpp:73`).
- [x] `PathCacheManager::expandTo` exists (`PathCacheManager.cpp:311`).
- [x] Path-level excludes baked into `pathLevelExcludes()`
  (`PathCacheManager.cpp:585-603`):
  - [x] `/System`
  - [x] `/private`
  - [x] `/dev`
  - [x] `/Volumes`
  - [x] `/cores`
  - [x] `/.fseventsd`
  - [x] `/.Spotlight-V100`
  - [x] `/.DocumentRevisions-V100`
  - [x] `/.PKInstallSandboxManager`
  - [x] `/.PKInstallSandboxManager-SystemSoftware`
  - [x] `/.Trashes`
  - [x] `/.TemporaryItems`
  - [x] `/.MobileBackups`
  - [x] `/.HFS+ Private Directory Data`
- [x] Excluded entries increment `m_foldersExcluded` (`PathCacheManager.cpp:699,710`).
- [x] `CacheStrategyTest::systemPathIsNotInCacheAfterScan` passes.
- [x] `CacheStrategyTest::privatePathIsNotInCacheAfterScan` passes.
- [x] `CacheStrategyTest::devPathIsNotInCacheAfterScan` passes.
- [x] `CacheStrategyTest::volumesPathIsNotInCacheAfterScan` passes.
- [x] `CacheStrategyTest::normalPathIsInCacheAfterScan` passes
  (sanity check that excludes aren't over-broad).
- [x] `CacheStrategyTest::expandToHandlesMultipleRoots` passes.
- [x] `CacheStrategyTest::expandToDeduplicatesAlreadyCovered` passes.

---

## TODO 4 — "Show hidden" must not re-index

- [x] `PathCacheManager::setShowHidden` is a no-op
  (`PathCacheManager.cpp:39-49`, comment confirms).
- [x] Scan workers use `QDir::Hidden` unconditionally.
- [x] `FolderSearchWorker::setIncludeHidden(bool)` exists
  (`FolderSearchWorker.h:34`, `FolderSearchWorker.cpp:93`).
- [x] `FolderSearchWorker::pathIsHidden(path)` static helper exists
  (`FolderSearchWorker.h:42`, `FolderSearchWorker.cpp:98`).
- [x] `FolderBrowserDialog::onShowHiddenToggled` updates
  `QFileSystemModel` filter, `PathSelector` adapter, and search worker
  (`FolderBrowserDialog.cpp:801-821`).
- [x] `onShowHiddenToggled` deliberately does NOT call
  `PathCacheManager::setShowHidden` to trigger a rescan
  (`FolderBrowserDialog.cpp:1210`).
- [x] `UsabilityTest::eyeToggleDoesNotRescan` (T-067b) passes.
- [x] `UsabilityTest::eyeToggleHidesHiddenSearchResults` (T-067c) passes.

---

## TODO 5 — First-run autostart prompt

- [x] `firstRunCompleted` key in `QSettings("Maude", "FolderBrowser")`
  (`src/Autostart.cpp:14`).
- [x] First-run modal dialog with text "Start macos-search automatically
  when you log in?" (`src/FirstRunDialog.cpp:21`).
- [x] Default focus on `[Yes, enable autostart]` button — verified by
  `dialogFocusOnEnableButton` and `dialogDefaultButtonIsEnable`.
- [x] Persist choice on close — `Autostart::applyFirstRunChoice` calls
  `markFirstRunCompleted()` unconditionally.
- [x] On Yes: persist `autostart=true` and register with OS at-login
  launcher (LaunchAgent plist).
- [x] On Skip: leave `autostart=false`.
- [x] `src/Autostart.{h,cpp}` namespace with `isEnabled()` /
  `setEnabled(bool)`.
- [x] **Dev/prod gate** — `Autostart::isProductionBuild()` returns false
  when binary is in a `/build*/` tree; the prompt is suppressed in dev
  mode so we don't accidentally register a transient build as the
  at-login app. Env vars `MACOS_SEARCH_FORCE_PROD=1` and
  `MACOS_SEARCH_DRY_RUN_AUTOSTART=1` provide a manual prod-path smoke
  test from a dev binary.
- [x] LaunchAgent plist at `~/Library/LaunchAgents/<bundle-id>.plist`,
  loaded with `launchctl load -w`.
- [ ] `SMAppService` path for macOS 13+ (deferred — LaunchAgent path
  works on all supported macOS versions; revisit if Apple deprecates).
- [x] Wired into `main.cpp` — `Autostart::firstRunNeedsPrompt()` checked
  after `dialog.show()`, prompt shown modal-on-top of main window.
- [x] Test: `firstRunPromptShownInProdWithFreshSettings`.
- [x] Test: `firstRunYesPersistsAutostartAndCompletes`.
- [x] Test: `firstRunSkipPersistsCompletedOnly`.
- [x] Test: `firstRunPromptShowsOnceOnly`.
- [x] Test: `firstRunPromptHiddenInDevMode` (new — covers the gate).
- [x] Test: `setEnabledIsNoOpAtOsLayerWhenDev`.
- [x] Manual smoke: dev launch shows no prompt; `MACOS_SEARCH_FORCE_PROD=1`
  + `MACOS_SEARCH_DRY_RUN_AUTOSTART=1` shows the prompt without touching
  `launchctl` (verified via screenshot 2026-05-18--22.27.56).

---

## TODO 6 — Global hotkey ⌃⌥⇧S

- [x] `src/GlobalHotkey.{h,cpp}` exists.
- [x] Links `-framework Carbon` (both app and tests, via CMakeLists).
- [x] Uses `RegisterEventHotKey` to register ⌃⌥⇧S
  (`GlobalHotkey.cpp:62-76`).
- [x] Emits `summonRequested` signal on chord
  (`carbonHotKeyHandler` → `QMetaObject::invokeMethod`).
- [x] On summon: show + raise + activateWindow
  (`FolderBrowserDialog::summon()`).
- [x] On summon: focus search field + selectAll.
- [x] On summon when already focused: idempotent re-focus + selectAll
  (calling summon while focused just re-selects the text).
- [x] Conflict handling for `eventHotKeyExistsErr` — `registerSummonChord`
  returns false on any non-`noErr` status, `qWarning`-logs the code, and
  main.cpp does **not** block startup on failure.
- [x] Wired in `main.cpp` — gated on `hotkeyEnabled` QSettings (default
  ON), `registerSummonChord()` called conditionally,
  `summonRequested` connected to `FolderBrowserDialog::summon`.
- [x] Shortcuts hint line mentions ⌃⌥⇧S
  (`FolderBrowserDialog.cpp:499`).
- [x] Test seam `GlobalHotkey::Testing::setDryRun` so unit tests don't
  grab the user's actual chord.
- [x] Test: `dryRunRegisterReturnsTrueWithoutCarbonCall`.
- [x] Test: `dryRunUnregisterClearsRegisteredFlag`.
- [x] Test: `registerIsIdempotent`.
- [x] Test: `summonSignalEmittedManually`.
- [x] Test: `summonInvokesDialogFocusAndSelectAll` (verifies the
  receiver-side contract: focus moves to search field, selectAll on
  pre-existing text).
- [x] Test: `shortcutsHintContainsSummonChord`.

---

## TODO 7 — Preferences menu item (depends on 5 + 6)

- [x] Preferences modal triggered from existing gear icon
  (`FolderBrowserDialog::onExcludeButtonClicked` now opens
  `PreferencesDialog`).
- [x] Checkbox: `Start macos-search automatically at login`
  (`PreferencesDialog::m_autostart`) → `Autostart::setEnabled(bool)`.
- [x] Checkbox: `Enable global hotkey ⌃⌥⇧S to summon the app`
  (`PreferencesDialog::m_hotkey`) → persists `hotkeyEnabled` QSettings +
  dispatches `GlobalHotkey::register/unregister` if the handle was set.
- [x] Checkbox: `Show hidden folders` — persists `showHidden` QSettings
  and emits `showHiddenChanged`; main dialog forwards to the eye button
  so the existing `onShowHiddenToggled` path keeps presentation in sync.
- [x] Flip-without-relaunch — autostart writes the plist and runs
  `launchctl load -w` immediately; hotkey calls register/unregister on
  the live `GlobalHotkey`; show-hidden re-runs the current query via
  the existing eye-toggle handler.
- [x] "Edit exclude rules…" button inside Preferences opens the
  existing `ExcludeSettingsDialog` (preserves the legacy entry point).
- [x] Test: `hasAllThreeCheckboxes` + `hasEditExcludesButton`.
- [x] Test: each `*InitiallyReflectsQSettings` slot.
- [x] Test: `togglingAutostartPersists`.
- [x] Test: `togglingHotkeyPersistsAndDispatches`.
- [x] Test: `togglingShowHiddenPersistsAndEmits`.
- [x] Test: `closeButtonAccepts`.
- [x] Test: `gearOnMainDialogOpensPreferencesNotExclude` — clicks the
  gear, finds the `preferencesDialog` child, closes it.

---

## TODO 8 — Dialog stays open after every "open" action

The dialog is the running app's main window. Closing it (via `accept()`
or hiding) effectively ends the session. Two paths used to call
`accept()` and have been fixed to "open + stay":

- [x] `onSearchResultDoubleClicked` — now opens the path via
  `/usr/bin/open` and keeps the dialog visible. Triggered by both
  mouse double-click and Enter-on-list (via `itemActivated`).
- [x] `onChooseClicked` (legacy) — delegates to `onOpenInAppClicked`,
  no `accept()`.
- [x] `QApplication::setQuitOnLastWindowClosed(false)` set in
  `main.cpp` as belt-and-braces: even if some path momentarily hides
  the window, the app keeps running. Exit is Cmd-Q only.
- [x] Regression test: `doubleClickSearchResultDoesNotCloseDialog`
  (T-094b).
- [x] Regression test: `enterOnSearchResultDoesNotCloseDialog`
  (T-094c).
- [x] Regression test: `chooseSlotDoesNotCloseDialog` (T-094d).
- [x] Manual smoke: launched app, typed query, pressed Enter on
  result — process stayed alive afterwards.

---

## Future / nice-to-have

### Cache strategy — Phase 2

- [ ] Scope pill in toolbar: `Indexed: ~ · Documents · /Applications`.
- [ ] Clicking scope pill opens favorites sidebar editor.
- [ ] "Index complete disk" one-shot button with confirm dialog +
  estimated time.

### UI polish

- [ ] Match highlighting in tree view (currently only the
  search-results list highlights in purple).
- [ ] Drag-and-drop a folder onto the sidebar to add as favorite.
- [ ] Quick Look (Space) to preview the selected file/folder.
- [ ] Per-row hover glyphs for reveal-in-Finder + open-with-App
  (in addition to the bottom buttons).

### Discoverability

- [ ] First-run intro pane explaining favorites sidebar, keyboard
  map, and privacy story ("I'll index your home folder, nothing
  leaves this Mac").

### Build & ship

- [ ] Universal binary (currently arm64-only).
- [ ] Notarized DMG with code signing.
- [ ] CI: GitHub Actions running `./br --test` on macOS-latest.

### Tests

- [ ] Right-click context-menu E2E without hanging on `QMenu::exec()`
  (today data effects are tested via `setDefaultFavorite` /
  `removeFavorite` direct calls).
- [ ] Tab-traversal test through every focusable widget in order
  (individual focus targets are tested but not full traversal).

### Drift / lift cleanup

- [ ] Decide whether to delete unused `MainWindow.{cpp,h}` +
  `SearchResultModel.{cpp,h}` (the app's top-level is
  `FolderBrowserDialog` directly).

---

## Decisions already made (no longer open)

- [x] macOS only. No Linux/Windows branches.
- [x] Standalone fork of `../maude-cp-v3`. No backport obligation.
- [x] Default scan strategy is favorites-driven; `/` is just another
  favorite with seeded path-level excludes.
- [x] Eye toggle is presentational only.
- [x] Home is the implicit default; `defaultFavorite=""` in
  `QSettings` means "use Home".
- [x] The persistent keyboard hint line lives at the bottom of the
  dialog and is the discoverability path for chords.
