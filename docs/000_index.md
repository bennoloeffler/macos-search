# Docs Index

The repo doc set, ordered for a new contributor.

## Specs (what the app is)

1. [010_vision.md](010_vision.md) — what the app does, for whom, non-goals.
2. [020_ux_contract.md](020_ux_contract.md) — built-in UX behavior (window, results, keyboard map, states).
3. [030_performance_budget.md](030_performance_budget.md) — latency / memory / throughput observed + targets.
4. [040_data_and_environment.md](040_data_and_environment.md) — what `$HOME` looks like in production, exclude defaults, FSEvents edges.

## How it's built

5. [050_porting_rules.md](050_porting_rules.md) — how the lift from `../maude-cp-v3` is structured and how to keep drift under control.
6. [060_test_strategy.md](060_test_strategy.md) — test runner, ported tests, coverage gaps. **101 tests across 6 classes.**
7. [070_build_and_ship.md](070_build_and_ship.md) — CMake, br.sh, bundle metadata, signing/distribution status.
8. [080_operations.md](080_operations.md) — logs, crash reporting, privacy posture.
9. [090_open_questions.md](090_open_questions.md) — what's still blocked on a Benno call.

## Day-to-day

10. [100_dev_workflow.md](100_dev_workflow.md) — compile / test / start / drive / screenshot loop, with macOS permission notes.

## Future features (proposed, not built)

11. [110_features_autostart_and_hotkey.md](110_features_autostart_and_hotkey.md) — autostart for warm cache; global hotkey ⌃⌥⇧S.
12. [160_navigation_history.md](160_navigation_history.md) — proposed `< >` back/forward through search contexts (root + filters), `⌘←` / `⌘→`. Four open questions still pending.

## Subsystem deep-dives

12. [120_qt_threading.md](120_qt_threading.md) — **Read before touching the cache, search worker, or ExcludeSettings.** Captures the `QReadWriteLock` pattern and the data race we hit.
13. [130_favorites.md](130_favorites.md) — sidebar layout, persistence, default favorite, first-run seeded defaults.
14. [140_keyboard_shortcuts.md](140_keyboard_shortcuts.md) — full keyboard map. All-key-without-mouse usable.

## Qt / C++ reference (imported from upstream)

Verbatim copies from `../maude-cp-v3/docs/`, encoding the same constraints
the lifted code follows.

See [qt-reference/README.md](qt-reference/README.md) for the index.
Highlights:

- `qt-reference/041_memory_management.md` — Crash #5 (QFileSystemModel
  background thread) explains why our dialog detaches the model in its
  destructor.
- `qt-reference/050_folder_search.md` — the cache+search architecture
  this repo implements.
