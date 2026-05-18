# Favorites Sidebar

The left sidebar of the dialog shows favorite root paths as Finder-style
cards. One favorite can be marked the "default" — that's the path the
app starts in.

## Visual model

- Section header: `FAVOURITES` (uppercase, small, gray).
- Each favorite is a **card** — soft background, 1 px subtle border,
  8 px corner radius, 10/12 px padding, 2 px spacing between cards.
- The **default** favorite is rendered **bold** (no bubble prefix).
- Hover state: brand-tinted background.
- Selected state: brand-color border.
- Below the list: `+ Add current` button.

`Home` is always the first row; it is implicit and cannot be deleted.

## Seeded defaults on first run

When `QSettings("Maude", "FolderBrowser")` has no `"favorites"` key
(fresh install or after `defaults delete`), the sidebar seeds with:

| Label | Path |
|---|---|
| Home | `~` (implicit, always present) |
| Documents | `~/Documents` |
| Downloads | `~/Downloads` |
| Desktop | `~/Desktop` |
| Macintosh HD | `/` |

Paths that don't exist on this machine are filtered out at render time,
so the sidebar never shows a dangling row.

Once seeded, the list is persisted; subsequent launches read the user's
current state. If the user removes all four defaults, the empty list is
persisted — they stay empty. There is no auto-re-seed.

## Interactions

| Input | Action |
|---|---|
| **Left-click row** | Set this path as the Search-in root + navigate to it. |
| **Right-click row** | Open mini-menu: `Make default` (if not already) · `Delete` (hidden for Home). |
| **Arrow ↑ / ↓** when focused | Move selection within the favorites list. |
| **`+ Add current` click** | Add the current `Search in:` path to favorites. Ignores `~` (implicit) and duplicates. |

### Default semantics

- The default is what `FolderBrowserDialog::resolveDefaultStartPath()`
  returns — used at app launch.
- "Make default" persists the path in `defaultFavorite`.
- "Make default" on Home stores `""` (empty marker) so that **deleting
  whichever favorite is currently default automatically falls back to
  Home**.

## Persistence

Two `QSettings("Maude", "FolderBrowser")` keys:

```ini
favorites=["/Users/benno/Documents", "/Users/benno/Downloads", "/", ...]
defaultFavorite=""   # empty → Home is the default
```

`""` for `defaultFavorite` is the explicit "Home is default" marker;
this avoids storing the user's home path literally (portability across
machines, account renames, etc.).

## Source of truth

- `src/FolderBrowserDialog.{h,cpp}`:
  - `loadFavorites()` / `saveFavorites()` — persistence + first-run seed
  - `rebuildFavoritesList()` — populates the QListWidget
  - `onFavoritesContextMenu()` — Make default / Delete menu
  - `addCurrentRootAsFavorite()` — the `+` button
  - `removeFavorite()` / `setDefaultFavorite()` — model mutations
  - `resolveDefaultStartPath()` — static, used by `main.cpp` at launch

Tests: `tests/FolderBrowserDialogTest.cpp` covers persistence,
non-existent paths, Home-is-always-present, and the seeded-defaults
contract.
