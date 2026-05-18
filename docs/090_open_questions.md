# Open Questions / Decisions Needed

Trimmed down from the speculative version: anything already decided was
moved into the topic doc; anything still genuinely open lives here.

## Blocks v1.0 — needs a Benno call

1. **App name in Finder.** Right now: literally "macos-search" (matches
   the repo name). Suggested alternatives: *PathFinder*, *Sucher*. Trivial
   to change — one string in `CMakeLists.txt`.
2. **Code signing path.** Three options, pick one:
   - (a) Stay unsigned. Internal V&S only. Users right-click → Open the
     first time.
   - (b) Apple Developer ID sign + notarize + staple. Required for any
     public download.
   - (c) Skip standalone distribution entirely, distribute as source.
3. **Distribution channel.** DMG drop in Dropbox? Homebrew cask? GitHub
   release? Drives a `scripts/package.sh`.
4. **(Resolved)** ~~Drift policy with `../maude-cp-v3`~~ — decision: this
   is a standalone fork. No backport obligation. See `050_porting_rules.md`.

## SHOULD answer before v1.1

5. **Sort order**: stay with `FolderSearchWorker`'s fuzzy score, or add
   a "folders-before-files" pass? Currently the former.
6. **Modified-date / size columns** in the result row, yes/no?
7. **Match highlighting** — port the purple-on-pink highlight delegate
   from upstream (separate ~150 LOC), or leave plain text?
8. **`PathSelector` widget** — port pass 2? Adds `SwiftUIStyle`,
   `MaudeConfig`, `ThemeManager` deps. Not needed for "just search"
   but the README screenshot includes it.

## NICE / future

9. **Autostart** — see `110_features_autostart_and_hotkey.md`. Pre-warm
   the cache via LaunchAgent or SMAppService.
10. **Global hotkey** (⌃⌥⇧S) — see same doc. Needs Carbon
    `RegisterEventHotKey` or a vetted wrapper.
11. **CI** — GitHub Actions running `./br --test`? Worth it once the
    repo gets multiple contributors.
12. **Manual rescan in UI** — toolbar button vs. menu item.
13. **Privacy intro pane** on first launch.

## What's already decided (and where it's documented)

- **macOS only.** Linux/Windows watchers stripped on import.
  → `050_porting_rules.md`.
- **Qt 6 + CMake + C++17.** → `070_build_and_ship.md`.
- **Bundle ID `de.v-und-s.macos-search`** (placeholder, see #1 above).
- **`$HOME` is the scan root.** Not Full Disk Access.
  → `080_operations.md`.
- **No telemetry, no phone-home.** → `080_operations.md`.
- **Two-tier keyboard semantics** — Enter opens, ⌘⏎ reveals.
  → `020_ux_contract.md`.
- **Test runner is a single aggregate binary**. → `060_test_strategy.md`.
- **`./br --test` is the local gate.** → `100_dev_workflow.md`.
