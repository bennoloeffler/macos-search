# Keyboard Shortcuts (full map)

The app is fully usable without a mouse. A persistent hint line at the
bottom of the window shows the most-used chords.

## Global chords (work from any focused widget in the dialog)

| Chord | Action |
|---|---|
| **⌘F** | Focus the `Search for:` field + select-all. |
| **⌘L** | Focus the `Search in:` path field. |
| **⌘ ⇧ G** | Focus the path field (legacy upstream binding — same as ⌘L). |
| **⏎** | Open the currently selected path with its default app. |
| **⌘⏎** | Reveal the currently selected path in Finder (`open -R`). |
| **⌘↑** | Go to the parent folder. |
| **⌘H** | Jump to Home. |
| **Esc** | Clear `Search for:` if non-empty. Never closes the dialog. |

## Typing

| Input | Effect |
|---|---|
| Any printable character | Appends to `Search for:` and gives it focus, regardless of which widget currently has focus (search field, tree view, favorites, …). Old "type replaces" bug is fixed. |
| Tab | Native focus traversal. |

## Result-list navigation (works even when the search field has focus)

| Key | Action |
|---|---|
| **↑ / ↓** | Move selection up/down. |
| **PgUp / PgDn** | Page up/down. |

The dialog forwards these keys from the search field into the visible
view (tree view when no query is typed, search-results list otherwise).
The favorites list keeps its own arrow handling and is **not** affected.

## Favorites sidebar

| Input | Action |
|---|---|
| **Left-click** a row | Set as Search-in root. |
| **Right-click** a row | Mini-menu: *Make default* · *Delete* (Delete hidden for Home). |
| **↑ / ↓** while focused | Move selection within the list. |

## Open / Reveal buttons

| Button | Equivalent chord |
|---|---|
| **Open with App** (primary, purple) | **⏎** |
| **Open in Finder** (secondary) | **⌘⏎** |

## What's deliberately NOT bound

- **⌘W** is not consumed — macOS default closes the window. We treat
  closing the window as quitting the app.
- **⌘Q** — macOS handles quit-app natively.
- **Cmd-Period** — no special meaning. Use Esc instead.

## Implementation

All keyboard handling lives in:

- `FolderBrowserDialog::keyPressEvent` — the global chord dispatcher
  and the printable-char redirector to the search field.
- `FolderBrowserDialog::eventFilter` (installed on the tree view) —
  intercepts printable keys so `QTreeView::keyboardSearch` doesn't
  swallow them.
- `PathSelector/PathSelectorKeyHandler.cpp` — handles Tab / Return /
  Esc / arrows inside the path field's completion popup.

Tests covering this surface live in
`tests/UserInteractionTest.cpp` — they simulate user keystrokes via
`QTest::keyClick` and assert outcomes.
