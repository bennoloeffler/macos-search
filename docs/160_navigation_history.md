# Proposed Feature — Navigation History (`< >` Back / Forward)

Browser-style back/forward through previous **search contexts**. Not
built yet; this doc exists so the design constraints don't get lost
before we implement it.

The concept came up in conversation, was sketched out in some detail
(below), and four open questions never got answered. Read those before
starting.

---

## Why

The user drills into a folder, types a query, switches to "Files",
adds an "inside contents" filter — then wants to go *back* to the
previous root + query without manually undoing every step. Today
the only undo is in the line edit (and only for the focused one).
Up-button (⌘↑) walks the **directory tree**, not the **search-state
history** — those are different things.

Finder, Safari, every IDE, every code-search tool has back / forward.
We don't yet.

---

## What to capture per `NavState`

A snapshot of everything that determines what the user is currently
looking at. Roughly:

| field            | type             | notes                                      |
|------------------|------------------|--------------------------------------------|
| `rootPath`       | `QString`        | the search scope                           |
| `searchMode`     | `SearchMode`     | `Folders` / `Files` / `Both`               |
| `searchQuery`    | `QString`        | the top search-field text                  |
| `contentQuery`   | `QString`        | the "inside contents" filter text          |
| `contentRegex`   | `bool`           | regex toggle for the content filter        |
| `showHidden`     | `bool`           | eye toggle state                           |
| `selectedPath`   | `QString` (opt.) | which row was current at push time         |

~200 bytes per entry. A 50-entry cap is ~10 KB — fine to keep in RAM
forever, no persistence.

---

## When to push

Push a new `NavState` onto the history stack on **deliberate
navigation moves**:

- Click on a folder row in the tree view (sets a new `rootPath`).
- Click on a file/folder row in the search results.
- Click on a favorite in the sidebar.
- Press `⌘↑` (Up to parent).
- Press `⌘H` (Home).

For things the user types into a `QLineEdit` (search field, content
field) — do **not** push on every keystroke. Two reasons:

1. The line edit has its own undo (`⌘Z`).
2. Pushing 12 states for typing `"invoice"` floods the stack and
   makes Back useless.

For toggle changes (mode pill, regex, show-hidden): debounce. After
**500 ms of stability** since the last toggle, push a single state.

## When NOT to push

- Inside the history-restore path itself. Guard with a
  `bool m_navigating` flag; while true, all the setters that would
  normally push are no-ops on the history stack.
- When the new state would be identical to the top of the stack.

## Branch truncation (browser model)

Standard browser semantics: if the cursor is mid-stack (user has
pressed Back N times), the next non-undo change truncates everything
ahead of the cursor before pushing the new state. No tree of
histories, no "forward" survives after a new navigation.

---

## UI

Two flat icon buttons immediately **left of the Up button** in the
navigation toolbar:

```
[ < ] [ > ]  [ ↑ ]  [ ⌂ ]   path-field …
```

- `<` — Back. Disabled when cursor is at the start of the stack.
- `>` — Forward. Disabled when cursor is at the end.
- Both 32×32, flat, same `secondaryButtonStyleSheet()` as the Up
  button. Reuse `IconRegistry` SVGs (chevron-left, chevron-right —
  the existing `up-arrow` SVG is a chevron and a left/right pair
  should drop in).
- Tooltips: `"Back (⌘←)"` / `"Forward (⌘→)"`.

## Keyboard

| chord     | action   | rationale                                           |
|-----------|----------|-----------------------------------------------------|
| `⌘←`      | Back     | Same chord everywhere (Safari, Chrome, every chat). |
| `⌘→`      | Forward  | Same.                                               |

**Why not `⌘[` / `⌘]`** — those require a dead-key Option on a
German Mac layout (`[` is typed `⌥5`, `]` is `⌥6`), so the user
would have to press `⌘⌥5` for Back. Three keys plus the option-as-
dead-key dance is too heavy. `⌘←` / `⌘→` is one chord on every
layout (US, German, French, Swiss, …) and matches the universal
browser/chat convention.

Confirmed no collision against the keymap in `140_keyboard_shortcuts.md`
— `⌘←` / `⌘→` aren't bound at the dialog level today.

**Trade-off acknowledged**: in a focused `QLineEdit`, Qt's default
for `⌘←` / `⌘→` is "move cursor to start / end of line". We override
that at the dialog's `keyPressEvent`. Acceptable because:
- Our line edits are short; Home / End keys still work for jump-to-end.
- Every browser and chat app overrides this same chord for back/forward —
  users already expect that behavior in any single-window app.

Out of scope for v1: trackpad swipe-back gestures (would need
`QGesture` work).

---

## Edge cases

- **Cap**: ring-buffer at 50 entries. Older entries fall off the
  bottom; current and future are preserved.
- **Persistence**: none. History resets on app launch — same as
  browser private mode. Cross-session persistence is overkill and
  introduces stale-path headaches.
- **Deleted paths**: if a `NavState`'s `rootPath` no longer exists at
  restore time, skip it (browser-style "404, jump to next entry").
  Two consecutive deleted entries in a row → fall back to `$HOME`
  and a fresh empty query.
- **`selectedPath` not found**: restore everything else and let the
  list land on its top row. Don't bail out of the restore.

---

## Open questions (need user input before implementing)

1. **Per-instance vs shared across summons.** If the user dismisses
   the window and re-summons with ⌃⌥⇧S, should they pick up the
   prior history or start fresh? My instinct: **keep it** —
   summon-and-dismiss is "minimize", not "quit".

2. **Should `Esc`-clear push a state?** Currently Esc clears the
   search query. If it pushes a state, Back restores the query the
   user just cleared. If it doesn't, the user has no way to go back
   to it after Esc. I'd vote **yes, push** — Esc is a deliberate
   reset and the user might regret it.

3. **Just `< >` buttons, or also a recent-locations dropdown?**
   Browsers and Finder both add a long-press dropdown on the Back
   button showing the last N entries. Strictly v2, but flag it now
   so the button widget gets picked accordingly (`QToolButton` not
   `QPushButton` if we want the popup).

4. **Keep Up (⌘↑) distinct from Back, or unify?** Up walks the
   filesystem parent chain; Back walks navigation history. They
   diverge as soon as the user clicks a favorite or types a new
   query. My instinct: **keep them distinct** — Finder does too —
   but worth saying out loud.

---

## Implementation sketch

New file: `src/NavigationHistory.{h,cpp}` — a `QObject` with:

```cpp
class NavigationHistory : public QObject
{
    Q_OBJECT
public:
    struct NavState {
        QString rootPath;
        FolderBrowserDialog::SearchMode searchMode;
        QString searchQuery;
        QString contentQuery;
        bool    contentRegex;
        bool    showHidden;
        QString selectedPath;
    };

    void push(const NavState &);
    bool canGoBack() const;
    bool canGoForward() const;
    NavState goBack();        // moves cursor, returns new state
    NavState goForward();

signals:
    void canGoBackChanged(bool);
    void canGoForwardChanged(bool);

private:
    QList<NavState> m_stack;
    int m_cursor = -1;
    static constexpr int kCap = 50;
};
```

Wired in `FolderBrowserDialog`:

- Owns one `NavigationHistory` instance.
- Connects `canGoBackChanged` / `canGoForwardChanged` to button
  `setEnabled`.
- Snapshot-and-push helper:
  `NavState currentState() const` → assembles the struct from
  current widget state. Called from each "deliberate navigation"
  slot listed above.
- Restore helper:
  `void restoreState(const NavState &)` — sets `m_navigating = true`,
  applies each field via the existing setters
  (`setCurrentRoot`, `setSearchMode`, `m_searchField->setText`,
  `m_contentField->setText`, `m_contentRegex->setChecked`,
  `m_showHiddenButton->setChecked`), then `m_navigating = false`.

500 ms debounce for toggle changes: shared `QTimer` started/restarted
on every toggle; on `timeout()` it captures `currentState()` and
pushes.

---

## Tests to write

In a new `NavigationHistoryTest` class — pure-data, no GUI:

- `pushIsRecordedAndCanGoBack`
- `goBackThenForwardReturnsToSameState`
- `branchTruncationOnNewPushMidStack`
- `noOpPushWhenIdenticalToTop`
- `capLimitsStackTo50`
- `canGoBackChangedSignalFires`
- `canGoForwardChangedSignalFires`

Plus integration tests in `UsabilityTest` (live dialog):

- `clickFolderPushesHistory` — click a tree row, Back button enables.
- `cmdLeftGoesBack` — programmatic state change, then
  `QTest::keyClick(dialog, Qt::Key_Left, Qt::ControlModifier)`
  (Qt maps `Qt::ControlModifier` to ⌘ on macOS),
  verify state restored.
- `cmdRightGoesForward` — Back then `Qt::Key_Right` with the same
  modifier, verify forward state restored.
- `cmdLeftInSearchFieldGoesBackNotLineStart` — focus the search
  field, type text, press ⌘←: assert the dialog navigated back,
  and the line-edit cursor did NOT jump to position 0. (Locks in
  the override of Qt's default line-edit chord.)
- `escClearAddsToHistory` (or NOT — depends on Open Question 2).
- `deletedPathInHistorySkipped` — push state with `/tmp/x`, delete
  the dir, go back, expect the *next* entry to be restored.
- `restoreDoesNotRecursivelyPush` — verify `m_navigating` guard.

---

## What this is NOT

- Not a tree-view "expand previously-opened paths" memory. That's a
  separate concern (tree state isn't part of `NavState`).
- Not a "recent searches" UI. Browsing back through history happens
  with the buttons / chords; we're not exposing it as a list anywhere
  (yet — see Open Question 3).
- Not persisted. Quit = clean slate.

---

## Status

**Not started.** This is a design sketch. Resolve the four Open
Questions, then implement. See also `docs/todos.md` if/when this
becomes a tracked TODO.
