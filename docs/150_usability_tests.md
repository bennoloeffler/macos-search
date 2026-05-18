# Usability Tests — Catalog

Comprehensive list of user-interaction invariants for the dialog.
Implemented in `tests/UsabilityTest.{h,cpp}` (or as additions to
`UserInteractionTest` where the existing slot covers it).

Conventions:

- **T-XXX** — stable test ID. Cite in commits ("fix T-042 regression").
- **Scenario** — single user action or state.
- **Expected** — observable outcome after the action.
- **Slot** — implementation function name in the test class.

Per-test fixture (unless noted):

- Tests run with `QT_QPA_PLATFORM=offscreen`.
- `init()` resets persisted favorites/default and `stopScan()`.
- Tests that need a dialog do `FolderBrowserDialog dialog(QDir::homePath())`
  + `prepare(dialog)`.

---

## A. Focus & traversal

| ID | Scenario | Expected | Slot |
|---|---|---|---|
| T-001 | App launches | `focusWidget()` is the `searchField` QLineEdit | `initialFocusIsSearchField` |
| T-002 | Tab from search field | Focus moves to next focusable widget (not the dialog itself) | `tabFromSearchFieldMovesFocus` |
| T-003 | Shift+Tab from search field | Focus moves backward to previous focusable widget | `shiftTabFromSearchFieldMovesBackward` |
| T-004 | Click favorite row → Tab | Tab returns focus to expected widget (search field) | `tabFromFavoriteReturnsToSearchField` |
| T-005 | `focusWidget()` is never null during normal operation | every user action keeps a focused widget | `focusWidgetIsNeverNull` |
| T-006 | Tab order is sensible: search-in → search-for → results/tree → favorites → buttons | sequence of `focusWidget()` after successive Tabs matches the expected list | `tabOrderIsSensible` |
| T-007 | ⌘F always lands on the search field, even from favorites | `focusWidget() == searchField` after the chord | `cmdFAlwaysLandsOnSearchField` |
| T-008 | ⌘L always lands on the path field | `focusWidget()` is the path-selector's QLineEdit | `cmdLAlwaysLandsOnPathField` |
| T-009 | After Esc clearing the search field, focus stays on the search field | not stolen by tree or favorites | `escClearKeepsFocusOnSearchField` |
| T-010 | Clicking a favorite row does NOT steal focus from the search field if it had focus | typing remains uninterrupted | `clickingFavoriteDoesNotStealSearchFocus` *(may need fix in code)* |

## B. Keystroke dispatch — global chords

| ID | Scenario | Expected | Slot |
|---|---|---|---|
| T-020 | ⌘F | search field focused + select-all | `cmdF_focusesAndSelectsAll` |
| T-021 | ⌘L | path field focused | `cmdL_focusesPathField` |
| T-022 | ⌘⇧G | path field focused (legacy alias) | `cmdShiftG_focusesPathField` |
| T-023 | ⌘H | resolved path becomes `$HOME` | `cmdH_jumpsToHome` |
| T-024 | ⌘↑ from `$HOME` | resolved path becomes parent of `$HOME` | `cmdUp_goesToParent` |
| T-025 | ⌘↑ from `/` | no crash; resolved path unchanged | `cmdUp_atRootIsNoOp` |
| T-026 | ⏎ anywhere | `onOpenInAppClicked` fires (selectedPath populated) | `enterTriggersOpenWithApp` |
| T-027 | ⌘⏎ anywhere | `onOpenInFinderClicked` fires | `cmdEnterTriggersOpenInFinder` |
| T-028 | Esc with non-empty search | search cleared, focus back on search field, dialog still visible | `escClearsAndKeepsDialog` |
| T-029 | Esc with empty search | dialog still visible (regression-lock) | `escEmptyDoesNotCloseDialog` |
| T-030 | ⌘W | dialog is hidden / closed via Qt's standard close shortcut | `cmdW_closesDialog` |
| T-031 | ⌘Q | application quits (Qt default) — we don't assert quit, just no-crash | `cmdQ_doesNotCrash` |

## C. Typing into the dialog

| ID | Scenario | Expected | Slot |
|---|---|---|---|
| T-040 | Type `abc` from tree-view focus | search field contains `abc` | `typeAppendsToSearchFromTreeFocus` |
| T-041 | Type `abc` from favorites focus | search field contains `abc` *(verify favorites doesn't eat it)* | `typeAppendsToSearchFromFavoritesFocus` |
| T-042 | Type `abc def` (multi-word) | search field contains `abc def` | `multiWordQueryTyped` |
| T-043 | German umlaut input | search field contains the umlaut chars | `umlautInputAccepted` |
| T-044 | `⌘F` then `xyz` | search field is `xyz` (select-all on ⌘F replaces existing text) | `cmdFThenTypingReplacesExisting` |
| T-045 | Type `/` while search field empty | the `/` is appended (not intercepted as path completion) | `slashTypedInSearchAppendsLiterally` |

## D. Arrow navigation

| ID | Scenario | Expected | Slot |
|---|---|---|---|
| T-050 | ↓ from search field, results visible | search-results `currentRow` moves down | `arrowDownInResultsList` *(covered)* |
| T-051 | ↑ from search field, results visible | `currentRow` moves up | `arrowUpInResultsList` *(covered)* |
| T-052 | PgDn from search field, results visible | `currentRow` advances by visible-page-size | `pgDnAdvancesResults` *(covered)* |
| T-053 | ↓ from search field, tree view visible | tree `currentIndex` moves down | `arrowDownInTreeView` *(covered)* |
| T-054 | ↓ from favorites list focus | favorites `currentRow` moves down — NOT forwarded | `arrowDownInFavoritesStays` *(covered)* |
| T-055 | Home / End in search field | text cursor jumps to start/end (default QLineEdit behavior, not intercepted) | `homeEndInSearchFieldNotIntercepted` |

## E. Mouse interactions

| ID | Scenario | Expected | Slot |
|---|---|---|---|
| T-060 | Click a favorite | resolved path == that favorite | `clickFavoriteSetsRoot` *(covered)* |
| T-061 | Click `+ Add current` while at a non-Home path | path is persisted in favorites | `addCurrentPersists` *(covered)* |
| T-062 | Click `+ Add current` at Home | nothing persisted (Home is implicit) | `addCurrentAtHomeIgnored` *(covered)* |
| T-063 | Click `+ Add current` for duplicate | no duplicate row | `addCurrentDuplicateIgnored` *(covered)* |
| T-064 | Click Up button at non-root | resolved path moves up | `upButtonGoesToParent` |
| T-065 | Click Up button at root | no-op, no crash | `upButtonAtRootIsNoOp` |
| T-066 | Click Home button | resolved path == `$HOME` | `homeButtonJumpsHome` |
| T-067 | Click Eye toggle | `m_showHidden` flips; persists in QSettings | `eyeToggleFlipsAndPersists` |
| T-068 | Single click on tree row | scope changes to that folder; `Will open:` updates | `singleClickTreeRowSetsScope` |
| T-069 | Double-click on tree row | descend (currentPath becomes that folder) | `doubleClickTreeRowDescends` |
| T-070 | Click Open in Finder | `selectedPath` populated; no crash | `openInFinderButton` *(covered)* |
| T-071 | Click Open with App | `selectedPath` populated; no crash | `openWithAppButton` *(covered)* |
| T-072 | Right-click Home favorite | menu has *Make default* (if not already), no *Delete* | `rightClickHomeNoDelete` |
| T-073 | Right-click non-default user favorite | menu has both *Make default* and *Delete* | `rightClickUserFavoriteFullMenu` |
| T-074 | Right-click current default | menu does NOT show *Make default* | `rightClickDefaultHidesMakeDefault` |
| T-075 | Choosing *Make default* on a row | that row becomes bold; persisted in `defaultFavorite` | `makeDefaultPersists` |
| T-076 | Choosing *Delete* on a non-default row | row disappears; persisted | `deleteRowPersists` |
| T-077 | Choosing *Delete* on the current default | row disappears AND `defaultFavorite` clears (falls back to Home implicitly) | `deletingDefaultFallsBackToHome` |

> T-072–T-077 are exercised at the data-mutator level (`setDefaultFavorite`,
> `removeFavorite`) plus a separate assertion that the menu is
> *populated* correctly when `customContextMenuRequested` fires.
> `QMenu::exec()` is not invoked in tests.

## F. View-stack & state transitions

| ID | Scenario | Expected | Slot |
|---|---|---|---|
| T-080 | Empty search | `viewStack` shows tree view | `emptyQueryShowsTree` |
| T-081 | Non-empty search | `viewStack` shows results list | `nonEmptyQueryShowsResults` |
| T-082 | Clear search → tree view returns | view-stack back to tree | `clearQueryReturnsToTree` |
| T-083 | Search with no matches | shows the non-selectable `No results found` row | `noResultsShowsPlaceholderRow` |
| T-084 | First result auto-selected | `currentRow == 0` after results arrive | `firstResultAutoSelected` |
| T-085 | `Will open:` reflects current selection | string contains the selected path | `willOpenReflectsSelection` |
| T-086 | Status label transitions | starts with `Starting…`, transitions to `Indexing…`, then `Ready…` | `statusLabelTransitions` |

## G. Suppression

| ID | Scenario | Expected | Slot |
|---|---|---|---|
| T-090 | Type printable into tree view | search field gets the char (tree's keyboardSearch suppressed) | `treeKeyboardSearchSuppressed` *(covered)* |
| T-091 | Esc with empty search | dialog stays visible (QDialog::reject suppressed) | covered by T-029 |
| T-092 | ⌘W in offscreen mode | dialog hide / close, but no app crash, no other windows affected | `cmdWDoesNotCrash` |
| T-093 | Modifier-key chord (e.g. ⌘F) when search field has focus | search field text unchanged | `chordOnFocusedFieldDoesNotInsertChar` *(covered as `typingDoesNotEatModifierChords`)* |
| T-094 | Repeated Esc | dialog still visible after N Esc presses | `repeatedEscNeverCloses` |

## H. Visual / discoverability

| ID | Scenario | Expected | Slot |
|---|---|---|---|
| T-100 | Shortcuts hint label is visible at the bottom | text is non-empty and contains `⌘`, `Esc`, `↵` | `shortcutsHintIsVisible` *(covered as `shortcutsHintIsVisibleAndMentionsMainChords`)* |
| T-101 | Up button has a tooltip | non-empty tooltip | `upButtonHasTooltip` |
| T-102 | Home button has a tooltip | non-empty tooltip | `homeButtonHasTooltip` |
| T-103 | Eye button has a tooltip | non-empty tooltip | `eyeButtonHasTooltip` |
| T-104 | Gear button has a tooltip | non-empty tooltip | `gearButtonHasTooltip` |
| T-105 | Default favorite renders bold (no bubble) | item font is bold; item text does NOT start with `●` | `defaultFavoriteIsBoldNotBubbled` |
| T-106 | Non-default favorites render non-bold | item font is not bold | `nonDefaultFavoritesAreNotBold` |
| T-107 | Search field has a non-empty placeholder | placeholder text present | `searchFieldHasPlaceholder` |
| T-108 | Search field clear-button is enabled | `isClearButtonEnabled()` true | `searchFieldHasClearButton` |

## I. Cross-action consistency

| ID | Scenario | Expected | Slot |
|---|---|---|---|
| T-110 | After `setDefaultFavorite(X)`, `resolveDefaultStartPath()` returns X | static helper agrees with runtime state | `resolveDefaultMatchesSetDefault` |
| T-111 | After deleting the current default favorite, next dialog instance starts at `$HOME` | regression-lock for "deleting default falls back to Home" | `deletingDefaultMakesHomeDefault` |
| T-112 | Sequence: `+ Add current` → Make default → delete | system is consistent (no orphan default in settings) | `addThenDefaultThenDeleteIsConsistent` |
| T-113 | Open dialog A, modify favorites, open dialog B | B sees A's changes | `favoritesPropagateAcrossDialogInstances` |
| T-114 | Favorites count + `+ Add current` button == always-non-zero focusable count | sidebar always has at least Home + the button | `sidebarAlwaysHasAtLeastHomeAndAddButton` |

## J. Performance / responsiveness (smoke only)

| ID | Scenario | Expected | Slot |
|---|---|---|---|
| T-120 | Dialog constructs in < 500 ms | wall-clock | `dialogConstructsQuickly` |
| T-121 | 1000 rapid keystrokes don't block the UI | event loop pumps between keystrokes; no hang | `rapidKeystrokesDoNotBlock` |

---

## Count

~80 tests across 10 categories. Many overlap existing
`UserInteractionTest` coverage (~ ⅓); the rest are net-new.

Existing `UserInteractionTest` will stay; net-new tests land in
`tests/UsabilityTest.{h,cpp}` so the two classes are split by
intent: `UserInteractionTest` = behavior-regression locks (the
bugs we hit), `UsabilityTest` = comprehensive UX invariants.

## Open implementation questions

1. **Q: How to assert "menu has X but not Y" without `exec()`?**
   Approach: extract the menu-building logic into a helper that
   returns a `QList<QAction*>` so tests can read the actions
   directly. Then `onFavoritesContextMenu` does `menu.exec()` of
   the built actions.
2. **Q: Tab traversal on offscreen platform — does focus
   actually advance?** Probably yes via `focusNextChild()`. Worst
   case we drive Tab via `QWidget::focusNextChild()` directly.
3. **Q: How to measure responsiveness for T-121?**
   `QElapsedTimer` around a typing loop; assert wall-clock < N ms.

## Estimated effort

- Catalog (this doc): done.
- Implement net-new tests: ~120 LOC test class + helpers.
- Bug fixes surfaced (likely): 3-6 small patches (T-001 initial
  focus, T-010 click-favorite-stealing-focus, possibly T-072–T-077
  if the menu helper extraction is invasive).
