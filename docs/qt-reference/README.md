# Qt / C++ Reference (imported from `../maude-cp-v3/docs/`)

These documents were lifted verbatim from upstream `maude-cp-v3` on
2026-05-18. They describe Qt-specific conventions and learnings that
apply to this standalone repo because most of the search-related code
was lifted unchanged. Imported as-is so we don't paraphrase someone
else's hard-won learnings; cross-reference them from `CLAUDE.md`.

Anything in these docs that references `MaudeConfig`, `MaudeLogger`,
or other classes not present in this repo refers to upstream-only
helpers. The principles still apply; the class names don't.

## Index

### General Qt / C++ patterns

- [040_qt_technology.md](040_qt_technology.md) — Qt stack choices, threading model, signal/slot rules.
- [041_memory_management.md](041_memory_management.md) — Parent-ownership rules, when to use smart pointers, RAII vs Qt object trees.
- [042_crash_prevention.md](042_crash_prevention.md) — Common crash patterns and how to avoid them.
- [043_implementation_learnings.md](043_implementation_learnings.md) — Accumulated "things we got wrong" debugging notes.

### UI

- [030_ui_guidelines.md](030_ui_guidelines.md) — Layout, spacing, typography conventions used in upstream.
- [031_dark_mode.md](031_dark_mode.md) — Dark-mode-tuned palette and `qApp->styleHints()` hooks.
- [032_icons.md](032_icons.md) — Icon registry, retina handling, SVG vs PNG.
- [033_stylesheets.md](033_stylesheets.md) — Qt stylesheet patterns; specifically *what to avoid* (e.g. cascading bugs).

### Testing

- [020_testing_strategy.md](020_testing_strategy.md) — Three-tier strategy (unit / greybox / manual UAT), QTest patterns.
- [021_testing_advanced.md](021_testing_advanced.md) — Test mocking, async signal-spy tricks, headless-`offscreen` platform.

### Subsystems we lifted

- [050_folder_search.md](050_folder_search.md) — **The folder-search architecture this repo implements.** Read first.
- [051_path_selector.md](051_path_selector.md) — Path Selector V2 spec (state machine, etc.). Relevant when pass-2 lifts the `PathSelector/` widgets.

## Drift note

Upstream may update these docs after the import date. To diff:

    diff -u ../maude-cp-v3/docs/050_folder_search.md docs/qt-reference/050_folder_search.md

If upstream diverges in a way that contradicts this repo's
implementation, **this repo's behavior is canonical** — the upstream
doc was true at import time, but we may have deliberately deviated.
Document deviations in the relevant `0XX_*.md` topic doc in
`docs/`, not in the imported files.
