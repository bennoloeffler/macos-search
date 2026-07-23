# RESTART-PROMPT — restart & diagnose macos-search

Paste this to a Claude Code session (or follow it yourself) when the app
misbehaves — hangs, freezes, spins, "does not react", or shows permission
dialogs. It restarts cleanly and self-diagnoses from the on-disk logs.

---

## 0. First rule when it "looks stuck": SCREENSHOT before assuming a freeze

A stalled scan is often **not** a code freeze — it's a macOS permission
dialog (TCC / iCloud / OneDrive / "access files") sitting *behind* the window,
waiting for a click the user hasn't given. Always:

```
./scripts/screenshot.sh stuck-check      # look for a hidden Allow/Don't-Allow dialog
```

If a dialog is up: that's the "stuck". Click it (or it needs the user to).
Only if there's **no** dialog do you have a real hang — go to §3.

---

## 1. Restart cleanly

```
./scripts/deploy.sh                 # build Release, sign (stable id), install, launch
./scripts/deploy.sh --reset-tcc     # + clear permission grants for a clean first-run
```

The app is signed with the stable self-signed identity **"macos-search Benno
Loeffler"**, so folder grants persist across rebuilds — after the one-time
Allow on Desktop/Downloads/Documents you should never see those again.

To force a **cold rebuild** of the index (drops the warm-start snapshot):

```
rm -f ~/.macos-search/index-v1.bin
```

---

## 2. The self-reflection log — read it FIRST

The app writes `~/.macos-search/health.log` every second from an independent
background thread (survives a frozen main thread). One line looks like:

```
2026-07-23T20:34:36  mem=230MB rss=482MB threads=23 scan=SCANNING folders=246140 files=1854474 main=ok
```

Quick triage:

```
tail -20 ~/.macos-search/health.log                 # what's happening now
grep STALLED ~/.macos-search/health.log             # main thread froze? for how long?
ls -t ~/.macos-search/health-stall-*.txt            # auto-captured frozen stacks
grep -oE 'folders=[0-9]+' ~/.macos-search/health.log | sed 's/folders=//' | sort -n | tail -1
```

- `main=ok` on every line → the UI is responsive; a frozen *counter* with
  `main=ok` just means a scan worker is reading a huge/slow dir (or blocked on
  a dialog — see §0). Not a UI freeze.
- `main=STALLED <ms>` → the UI **is** frozen. A `health-stall-*.txt` was
  written at that moment — open it (§3).

---

## 3. Diagnose a real freeze (main=STALLED, no dialog)

Read the auto-captured stack — the frozen main-thread frames pinpoint it:

```
NEWEST=$(ls -t ~/.macos-search/health-stall-*.txt | head -1)
awk '/DispatchQueue_1: com.apple.main-thread/{f=1} f&&/^Thread [0-9a-fx]+ /&&!/main-thread/{f=0} f' "$NEWEST" \
  | grep -iE 'indexNewSubtree|onDirectoryChanged|diffDirectory|ingestListing|count|entryList|QReadWriteLock|__psynch' | head
```

Or sample the live process:

```
PID=$(pgrep -x macos-search)
sample $PID 4 -mayDie 2>/dev/null | awk '/main-thread/{f=1} f' | head -40
```

**Known freeze signatures and their fixes (all already fixed — check for
regressions):**

| Frozen in | Cause | Fix that shipped |
|---|---|---|
| `PathStore::count` / `QReadWriteLock` | label poll took the store read lock, starved by scan workers | `count()` is lock-free (atomic) |
| `ingestListing` for minutes on warm start | O(total-nodes) child lookup per dir | walks the child chain (O(#children)) |
| deep `indexNewSubtree` recursion | subtree walk ran on the main thread | offloaded to `scheduleSubtreeIndex` (pool) |
| `onDirectoryChanged → entryList` (13 s) | per-dir FS diff ran on the main thread | offloaded to `diffDirectory` (pool) |

---

## 4. Common non-freeze symptoms

- **Permission-dialog storm** (iCloud/OneDrive/Reminders/Contacts): the scan
  followed a symlink into a cloud/`~/Library` location. Fixed by `NoSymLinks`
  + `~/Library` exclude; if it recurs, a new symlink target leaked — check
  `ls -la ~ | grep '\->'`.
- **Counter caps at exactly 1,000,000 / 5,000,000**: that's the folder / file
  soft cap. Raise `kDefaultSoftCap` in `PathCacheManager.h` / `FileCacheManager.h`.
- **Count grows on every warm restart** ("counts MORE the second time"): the
  index inflates ~70% per launch. Root cause was an ORDERING race —
  `tryLoadSnapshot()` (which `m_nodes.swap()`s the whole store) ran AFTER the
  dialog constructor had already started a scan on the empty store; the scan
  kept stale node indices and re-added every child. Fixed by loading the
  snapshot BEFORE constructing the dialog (main.cpp). To check: cold-scan, note
  the count, then warm-restart 2–3× — it must stay flat. Trace with
  `MSEARCH_TRACE_SCAN=1` and look for `ADD` lines with high `node=` numbers on a
  warm start (those are freshly-appended duplicate subtrees). Regression-locked
  by PathStoreTest `warmReconcileViaWalkDoesNotInflate`.
- **Memory keeps climbing to GB**: check `bytesUsed()` per entry; the store
  should be ~40 B/entry. A climb means duplicate entries (e.g. macOS firmlink
  paths `/System/Volumes/Data/...` mirroring `/Applications`, `/Users`).
- **Huge/cryptic dirs indexed**: dirs with 10k+ entries (caches, git objects)
  are skipped by the entry-count heuristic (`maxDirChildren`, default 10'000,
  resets every launch); if they appear, that guard broke.
- **"…would like to access data from other apps" (AppData TCC) prompt + scan
  stuck on "Indexing…"**: a scan worker reached `~/Library/Containers/<app>` and
  blocked in `opendir()` on the modal, so the active-worker count never hit zero
  and `finishScan` never ran (`main=ok` the whole time — the UI was fine). Root
  cause was a **leading double slash**: scanning the "Macintosh HD" (`/`)
  favorite built `//Users/...`, and every single-slash path-level exclude then
  failed to match, so `~/Library` was walked. Fixed by the root-aware
  `joinPath()`. To catch a regression, run the app with `MSEARCH_TRACE_SCAN=1`
  and watch stderr — the LAST `SCAN <path>` line before it hangs is the culprit
  dir (this is how it was found):

  ```
  MSEARCH_TRACE_SCAN=1 /Applications/macos-search.app/Contents/MacOS/macos-search 2>&1 | grep '^SCAN '
  ```

  Invariant now regression-locked by `rootScanNeverProducesDoubleSlash` — no
  cached path may start with `//`.

Note: `sample <pid>` and `lldb -p` themselves pop a **"Developer Tools Access"**
password prompt (macOS debug entitlement) — that's the diagnostic tool, NOT the
app under test. Don't confuse it with an app-triggered TCC dialog.

---

## 5. Where things live

- Index snapshot: `~/.macos-search/index-v1.bin`
- Health log + stall dumps: `~/.macos-search/health.log`, `health-stall-*.txt`
- Deploy + signing: `scripts/deploy.sh`
- Dropbox release: `VundS Dropbox/VundS/B_Org Shop/B_05_IT/B_525_KI/macos-search-releases/`
- Everything else: `CLAUDE.md` → "Release & operations"
