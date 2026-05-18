# Data & Environment

What "realistic input" looks like for development and testing.

## Representative `$HOME`

`TBD` — capture from Benno's actual machine on the next pass:

- Total entries (files + folders).
- Folder count (the thing the cache actually stores).
- Max depth.
- Distribution of path-component count.

Until then, the bundled test fixture should at minimum exercise:

- Emoji in names.
- Spaces and German umlauts in names.
- NFD vs NFC unicode (macOS Finder writes NFD on HFS+, NFC on APFS — both appear in the wild).
- Very long absolute paths (> 1000 chars).
- Symlink loops.
- Permission-denied subtrees (e.g. `~/Library/Caches/com.apple.<something>`).
- Case-only-different siblings (`Foo/` and `foo/`).
- Files with newlines or tab characters in their names (rare but legal).

## Default exclude list

`ASSUMED` v1 — should match upstream behavior, simplified:

```
~/.Trash
~/Library/Caches
~/Library/Containers/*/Data/Library/Caches
~/Library/Application Support/*/Caches
~/.git (anywhere)
node_modules (anywhere)
.DS_Store
*.app/Contents  (don't recurse INTO .app bundles — treat as opaque)
Time Machine local snapshots
iCloud "placeholder" files that haven't downloaded yet
```

Hidden directories (starting with `.`): the cache always indexes them; the eye toggle is purely presentational (see `docs/todos.md` TODO 4).

## FSEvents edge cases

Things to watch for as the FSEvents watcher matures here:

`TBD` capture:

- Events delivered for items the watcher already excluded.
- Coalesced events that drop the actual changed path.
- Race between BFS finishing and FSEvents first event.
- Behavior on external-volume mount/unmount.
- Behavior on macOS sleep/wake.

## What we explicitly do NOT touch

- iCloud Drive items that aren't on disk (don't trigger download).
- Spotlight metadata DB. We have our own cache.
- Anything outside `$HOME` by default.
