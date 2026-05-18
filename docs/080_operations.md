# Operations (today + intent)

## Logging — today

The app prints `qDebug()` / `qWarning()` to stderr. With `./br --detach`
that goes into `build*/macos-search.log`. With direct execution
(`./br` default), it lands in the calling terminal. Foreground GUI launch
via `open` routes to Console.app (filter by process name).

There is **no structured logger**, no log rotation, no log location under
`~/Library/Logs`. Upstream `maude-cp-v3` has a full `MaudeLogger` — it
was deliberately not ported because of dependency weight. If support
requests start needing logs from non-developers, that's the time to port.

## Crash reporting

- macOS's built-in `ReportCrash` writes to `~/Library/Logs/DiagnosticReports/`.
  That's the only crash story today.
- No Sentry / Crashlytics. No third-party telemetry of any kind. The
  app does not phone home.

## Privacy & permissions

The app reads `$HOME` recursively. On first scan, macOS prompts for
access to `Desktop`, `Documents`, `Downloads` per-app. There is no
custom first-launch onboarding modal yet — the user sees the system
prompts directly. Acceptable for the developer / internal stage; before
shipping to less technical users, write a one-pane intro.

The app does **not** request Full Disk Access. The scan root is `$HOME`
and FSEvents is scoped to the same; both work without FDA.

## Telemetry

None. Not planned.

## Support story (today)

- "How do I send Benno a log?" — copy `build*/macos-search.log` and
  paste it into chat. (Needs a menu item when the app reaches
  non-developer users.)
- "How do I force a rescan?" — not exposed in UI. Today only via
  rebuilding (`./br -c`). Add a menu item / button when needed.
- "How do I uninstall?" — drag the `.app` to Trash. Delete
  `~/Library/Preferences/de.v-und-s.macos-search.plist` to nuke the
  `ExcludeSettings`-managed prefs.

## What's tracked but not built

- `~/Library/Logs/macos-search/` log destination.
- "Send log to Benno" menu item.
- Manual rescan toolbar button.
- First-launch privacy intro pane.
