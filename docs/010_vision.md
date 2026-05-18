# Product Vision

> This doc was rewritten on 2026-05-18 from "speculative" (lots of `TBD`) to
> "describes what is built". `090_open_questions.md` still holds open items.

## In one sentence

Type a few characters, get any file or folder under `$HOME` listed, click once
to open it in its default app or reveal it in Finder.

## Who is "DOW"

A non-technical knowledge worker on macOS (Apple Silicon by default).
"DOW" is the placeholder the README uses. They:

- Don't think in regex.
- Mostly look for **folders by project / customer name**.
- Secondarily look for individual documents (`.pdf`, `.docx`, `.xlsx`, `.pptx`).
- Treat anything that takes more than two clicks as "broken".

## User stories actually supported today

1. **Multi-term contains search.** Type two words separated by a space —
   both must match the path (case-insensitive, contains, anywhere in the
   path). Example: `trafo 2024` finds `Projects/Trafo 2024 - Kunde X`.
2. **One-click open.** Enter or double-click on a row opens the item in its
   default app (folders open in Finder, files in their associated app).
3. **One-click reveal.** ⌘⏎ on a row reveals the item in Finder
   (`open -R <path>`), selecting it inside its parent folder.
4. **Copy path.** ⌘C copies the absolute path of the focused row.
5. **Focus shortcut.** ⌘L focuses the search field at any time.
6. **Right-click menu.** Open · Reveal in Finder · Copy Path.
7. **Live cache.** Filesystem changes under `$HOME` (new folders, renames,
   deletes) propagate into the cache via `QFileSystemWatcher` and the next
   search reflects them — no rescan needed for normal use.

## Explicit non-goals (still)

- Content search (Spotlight-style full-text). Names and paths only.
- Regex / advanced query syntax. Multi-term space-separated AND is the
  whole grammar.
- Saved searches / smart folders.
- Global hotkey to summon the app. Tracked as future work in
  `110_features_autostart_and_hotkey.md` — not built.
- Indexing outside `$HOME`. The cache supports an arbitrary root via
  `setExcludeSettings()` and `expandTo()`, but the standalone app only
  wires up `$HOME` for now.
- Cross-platform. macOS only. The Linux / Windows watcher branches in
  upstream got stripped on import — see `050_porting_rules.md`.

## What "much better than Cmd-Shift-G" delivers

`Cmd-Shift-G` (Finder's "Go to Folder"):
- Only suggests **siblings** of the current path component.
- Has no fuzzy / multi-term match.
- Has no warm cache — every invocation re-reads the directory.

This app:
- Returns a **flat ranked list** of every match anywhere under `$HOME`.
- Multi-term AND with subfolder-of-match suppression (prevents result
  spam when one match has many descendants).
- Warm in-memory cache, so steady-state search is sub-50 ms regardless
  of how big `$HOME` is.
- Two explicit click targets: open or reveal.
