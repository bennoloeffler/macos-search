# UX Contract (as built)

Reverse-engineered from `src/FolderBrowserDialog.cpp` and verified by
`tests/UserInteractionTest.cpp`. Updated 2026-05-18.

## Window

- Top-level `QDialog` shown modeless (`setWindowFlag(Qt::Window, true)`).
- Title bar: `Open Project / Folder`.
- Internal title: `Select Folder` (centered).
- Initial size: 880 × 620. Minimum: 720 × 520.
- Closing the window (red traffic-light, ⌘W) quits the app.

## Top toolbar

Left → right:

- **Up** button (chevron-up icon) — go to parent directory; disabled at root.
- **Home** button (house icon) — jump to `$HOME`.
- **Indexing… N folders** / **Ready — N folders indexed** status (green when complete, orange while indexing).
- *(spacer)*
- **Eye** toggle (top-right) — show hidden folders; persisted.
- **Gear** button (top-right) — open the Exclude Patterns dialog.

## Favorites sidebar (left)

See `130_favorites.md` for the full design. In short:

- `FAVOURITES` section header.
- Each entry is a card; the default is **bold**; Home is implicit and always first.
- Seeds on first run with `Home, Documents, Downloads, Desktop, Macintosh HD` (non-existent ones filtered).
- `+ Add current` button at the bottom.
- Left-click → set as Search-in root. Right-click → mini-menu: *Make default* · *Delete* (Delete hidden for Home).

## Search row

- **Search in:** path field. Type a path, or press `/` or `↓` to open the completion popup. ⌘L focuses it.
- Hint line under the field: `/ or ↓ = show folders list`.

## Query row

- **Search for:** `QLineEdit` with native macOS clear button (×).
- Placeholder: `Type to search folders...`.
- Debounce: **150 ms** (via `SearchField` — the dialog uses an internal `QLineEdit` here, but the debouncing is replicated).
- ⌘F focuses it + selects-all.

## View area (right of sidebar, below query row)

Stacked widget:

- **Tree view** (`QFileSystemModel`) when the query is empty — folders only, single name column, animated expand/collapse, hidden columns for size/type/date.
- **Search results list** (`QListWidget`) when the query is non-empty:
  - Each row: 28-px purple-pill **count badge** on the left, then the path with **multi-term purple highlighting** on the right.
  - Empty result list shows a non-selectable `No results found` row.
  - First hit auto-selected so the *Will open:* preview reflects it immediately.
- The dialog forwards `↑ / ↓ / PgUp / PgDn` from the search field to whichever view is visible (see `140_keyboard_shortcuts.md`).
- Tree view's built-in `keyboardSearch` is suppressed via event filter — printable keys go to the search field instead.

## Footer

Three rows, top to bottom:

1. **Will open: /path/to/selected** — live preview of what Open buttons will act on.
2. **Persistent keyboard hint** — `↑↓ nav · ↵ open · ⌘↵ reveal · ⌘F search · ⌘L path · ⌘↑ up · ⌘H home · Esc clear`.
3. **Buttons**: `Open in Finder` (secondary, gray) and `Open with App` (primary, purple).

## Click / keyboard semantics

| Input | Action |
|---|---|
| Single click on tree row | Select the folder + scope the search to it. |
| Double-click on tree row | Descend into the folder (`navigateTo`). |
| Single click on search result | Set as scope. |
| Double-click on search result | Open with default app. |
| **Enter** (anywhere) | Open with default app. |
| **⌘Enter** | Reveal in Finder (`open -R`). |
| **⌘L** | Focus path field. |
| **⌘F** | Focus search field + select-all. |
| **⌘↑** | Parent folder. |
| **⌘H** | Home. |
| **Esc** | Clear search; never closes the dialog. |
| **Right-click favorite** | Mini-menu (Make default / Delete). |
| Printable char | Append to search field; replaces wrong "replace whole text" upstream bug. |

Full keyboard reference: `140_keyboard_shortcuts.md`.

## States

| State | Visible cue |
|---|---|
| Starting | toolbar status: `Starting…` |
| Indexing | orange `Indexing… N folders (M excluded)` |
| Indexing complete | green `Ready — N folders indexed (M excluded)` |
| Ready, no query | tree view of current path |
| Ready, query, hits | results list with badges + highlighting |
| Ready, query, no hits | `No results found` row |
| Cache mutation (FSEvents) | live update — active query re-runs silently |

## What's deliberately NOT in the UI

- No modal "OK / Cancel" — the dialog is the running app.
- No drag-and-drop from results (yet).
- No Quick Look on Space (yet).
- No global hotkey to summon (see `110_features_autostart_and_hotkey.md`).
- No multi-select.
