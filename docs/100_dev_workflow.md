# Dev Workflow — compile / test / start / drive / screenshot

The standalone GUI app needs a deterministic way to be driven and inspected
from a shell (and from Claude), without nuking unrelated processes. That
workflow is built from four small scripts under `scripts/`.

## The pieces

| Script | Purpose |
|---|---|
| `./br`              | Build & run. `--test`, `--detach`, `--no-debug`, `-c`, `-j N`, `--who=benno\|claude`. |
| `scripts/screenshot.sh` | Window-rect capture of any named app. Output in `screenshots/`. |
| `scripts/ui-drive.sh`   | Activate / type / key / chord / quit. Refuses to send keys when target app isn't running. |

All three are documented via `--help`.

## Canonical loops

### A. Just build, run tests

    ./br --test                # builds (if needed) and runs all QTest tests

Exits with the test runner's status. 60 tests at the time of writing
(`ExcludeSettingsTest`, `PathCacheManagerTest`, `SearchFieldTest`).

### B. Build, run, screenshot the empty window

    ./br --detach              # launches in background, prints PID, returns
    ./scripts/screenshot.sh empty-window
    ./scripts/ui-drive.sh quit

### C. Build, run, type a query, screenshot, quit

    ./br --detach
    sleep 0.5
    ./scripts/ui-drive.sh type "trafo"
    sleep 0.8                  # cover SearchField debounce (150 ms) + render
    ./scripts/screenshot.sh trafo-results
    ./scripts/ui-drive.sh quit

## Persistent index snapshot

The cache warm-starts from a snapshot written to
`~/.macos-search/index-v1.bin` (see `docs/210_persistent_index.md`). It
is saved after each completed scan root and on quit, and loaded at
startup before scanning. It's a pure accelerator — **deleting it is
always safe** (the next launch cold-scans). A change to the exclude
patterns or cap values changes the fingerprint, so a stale snapshot is
ignored automatically. `--bench` also writes it (harmless); trash it if a
bench run left temp-tree paths behind: `trash ~/.macos-search/index-v1.bin`.

### D. Crash / log inspection

    ./br --detach --log=/tmp/macos-search.log
    # ... do stuff ...
    tail -50 /tmp/macos-search.log
    ./scripts/ui-drive.sh quit

### E. Memory benchmarking

    ./build/macos-search.app/Contents/MacOS/macos-search \
        --bench --bench-root ~/projects --bench-queries 50 > report.json

The `--bench` JSON report contains a `memory` object sampled via mach
`task_info(TASK_VM_INFO)` at three points — `baseline` (before the scan),
`after_scan`, `after_queries` — each as `*_footprint` (`phys_footprint`,
what Activity Monitor shows) and `*_resident` (classic RSS). The derived
`bytes_per_entry` is the scan footprint delta divided by
`folder_count + file_count`; a one-line summary also goes to stderr.
These are the numbers behind gates G2/G4 in `docs/200_pathstore_redesign.md`.

To compare two builds (e.g. baseline vs. a memory-reduction branch):

    scripts/mem-compare.sh <binary-A> <binary-B> [--root PATH] [--queries N]

Runs both with `--bench` on the same root and prints entries, B/entry
for each, and the reduction factor A/B (~1.0 when A == B). Raw JSON
reports are kept in a temp dir whose path is printed. `--help` for details.

## Safety properties

These come up because the harness is automated:

- `ui-drive.sh quit` only quits the **named app** (and only by name or PID).
  It never sends a blind `Cmd-Q` to whatever's frontmost — an earlier version
  did and quit an editor window when macos-search had already exited.
- `ui-drive.sh type / key / chord` refuse to run if the target process isn't
  running (no blind keystrokes to whatever's frontmost — exit code 3).
- `screenshot.sh` falls back to full-screen capture if it can't resolve the
  target's window bounds, so it never produces an empty file silently.
- `br.sh --clean` uses `trash`, falling back to a `.claude-backup` rename;
  never `rm -rf`.

## macOS permission requirements

Two separate permission tiers come into play:

| Tier | What it grants | Granted automatically? |
|---|---|---|
| **AppleEvents to System Events** | Read window count, bounds, hierarchy. Used by `screenshot.sh`. | Yes — on first use macOS prompts once per terminal. |
| **Accessibility** | *Synthesize* keystrokes / clicks via System Events. Used by `ui-drive.sh type/key/chord`. | **No.** The responsible parent app must be granted manually. |

### What "responsible parent app" means

macOS attributes the TCC permission check to the **GUI app at the top of
the process chain** — not to the immediate parent of `osascript`. For
this repo, the chain when Claude Code runs a Bash tool is:

    VS Code → Code Helper → zsh → claude (CLI) → zsh → osascript

The responsible process is **`com.microsoft.VSCode`**, confirmed by
inspecting the system log:

    /usr/bin/log show --last 1m --style compact 2>/dev/null \
        | grep -E 'TCC.*AUTHREQ_ATTRIBUTION'

You'll see an entry like:

    AUTHREQ_ATTRIBUTION: ... attribution={
        responsible={TCCDProcess: identifier=com.microsoft.VSCode, ...},
        accessing={TCCDProcess: identifier=com.apple.osascript, ...},
        ...}
    AUTHREQ_RESULT: ... authValue=2     # 2 = allowed, 0 = denied

So: **grant Accessibility to the GUI terminal app** (VS Code, iTerm,
Terminal.app, etc.) hosting your shell, not the `claude` CLI binary.
The CLI inherits responsibility from its grandparent GUI.

### Granting Accessibility

  System Settings → Privacy & Security → Accessibility → `+` → add
  Terminal.app / iTerm.app / Visual Studio Code.app.

### Diagnosing "silent keystroke" issues

If `ui-drive.sh type "foo"` returns success but the screenshot doesn't
show "foo" in the field, the cause is usually **one** of:

1. **Accessibility denied** for the responsible app. Check the log
   predicate above — `authValue` will be `0`.
2. **Wrong focus** in the target app. The keystroke is delivered, but
   no editable widget has key focus. Check by clicking the field
   manually first, then re-running the script.

In this repo, (2) was the actual cause of the first round of "typing
doesn't work" — `SearchField` was missing `setFocusProxy(m_lineEdit)`,
so `m_searchField->setFocus()` in `MainWindow` parked focus on the
outer `QWidget` instead of the inner `QLineEdit`. Fixed in
`src/SearchField.cpp`.

`screenshot.sh` does **not** need Accessibility — only AppleEvents —
so the screenshot half of the workflow works on a fresh machine
without any permission grants.

## Why detached launch instead of just `open`?

`open path/to/macos-search.app` works but routes logs to Console.app — hard
to grep. `br.sh --detach` runs the bare binary with `nohup` and captures
stdout+stderr into `build*/macos-search.log`, which gets you `qDebug()`
output for free.
