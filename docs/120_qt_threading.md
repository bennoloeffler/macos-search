# Qt Threading — What We Learned

This doc is a focused write-up of the Qt threading rules as they apply to
this app, recorded after fixing a real `SIGSEGV` rooted in a cross-thread
data race. **Read it before touching `PathCacheManager`, `ExcludeSettings`,
`FolderSearchWorker`, or any class that talks to background workers.**

For the deeper Qt-side context, see also `qt-reference/041_memory_management.md`
("Crash #5: QFileSystemModel Background Thread") and the upstream Qt docs
linked at the bottom.

---

## The cardinal rule

> A `QObject` has **thread affinity** — it belongs to the thread that
> created it (or to whichever thread it was `moveToThread()`'d into).
> Non-thread-safe state inside that QObject must only be accessed by
> the owning thread, OR be explicitly protected by a lock.
>
> **All `QWidget` methods must run on the main (GUI) thread.** No exceptions.

Cross-thread communication uses one of:

| Mechanism | Use when |
|---|---|
| `Qt::QueuedConnection` signals | One-way notification, fire-and-forget |
| `QMetaObject::invokeMethod(obj, ..., Qt::QueuedConnection)` | Cross-thread method call |
| `QMutex` / `QReadWriteLock` | Shared state with both readers and writers |
| `QAtomicInt` / `QAtomicPointer` | Single primitive that needs lock-free read |

If your code does any of these from a background thread, something is
wrong:

- Direct `widget->setText(...)` — must be on the GUI thread.
- Direct read of a non-atomic member while another thread writes it — race.
- Direct `delete` of an object while events for it are in flight — UAF.

---

## The bug we hit (2026-05-18)

**Symptom.** `SIGSEGV` deep inside `ExcludeSettings::shouldExclude` at line 83:

```
Thread 11 Crashed:: QThread
0  QHashPrivate::iterator::isUnused() const  (qhash.h:791)
1  QHashPrivate::Data::begin() const         (qhash.h:631)
2  QHash::begin() const                      (qhash.h:1239)
3  QSet<QString>::begin() const              (qset.h:141)
4  ExcludeSettings::shouldExclude(QString const&) const  (ExcludeSettings.cpp:83)
5  PathCacheManager::scanWorker()            (PathCacheManager.cpp:649)
```

**Root cause.** The scan worker reads `m_enabledPatterns` (a `QSet<QString>`)
from background threads. The main thread can mutate the same set via the
exclude-patterns dialog (`addPattern`, `setPatternEnabled`,
`resetToDefaults`) at any moment. Without a lock, the QSet's internal
QHash gets corrupted under concurrent read/write — the iterator
dereferences a freed bucket and the process dies.

**Bad fix (we did this first).** Stop the scan in `aboutToQuit`. That
prevented the on-exit case but did nothing for the at-runtime case —
the user opening the exclude-patterns dialog mid-scan would have hit
the same bug.

**Good fix.** Add a `QReadWriteLock` to `ExcludeSettings` and gate
every access. See `src/ExcludeSettings.{h,cpp}`.

---

## The lock pattern we use

`ExcludeSettings` has many readers (N worker threads in
`PathCacheManager`) and few writers (rare user actions). That's a
textbook `QReadWriteLock`:

```cpp
#include <QReadLocker>
#include <QWriteLocker>

class ExcludeSettings : public QObject {
    // ...
private:
    mutable QReadWriteLock m_lock;   // mutable → const methods can lock
    QStringList m_patterns;
    QSet<QString> m_enabledPatterns;
};

// Readers: cheap, allow concurrent readers
bool ExcludeSettings::isPatternEnabled(const QString &p) const {
    QReadLocker locker(&m_lock);
    return m_enabledPatterns.contains(p);
}

// Writers: exclusive
void ExcludeSettings::addPattern(const QString &p) {
    QString trimmed = p.trimmed();
    bool changed = false;
    {
        QWriteLocker locker(&m_lock);
        if (trimmed.isEmpty() || m_patterns.contains(trimmed)) return;
        m_patterns.append(trimmed);
        m_enabledPatterns.insert(trimmed);
        changed = true;
    }
    // Emit signals OUTSIDE the lock — slot might callback into us.
    if (changed) {
        save();              // QSettings is itself thread-safe
        emit patternsChanged();
    }
}
```

### Two non-obvious rules we follow

1. **Don't emit signals while holding the lock.** A slot connected to
   `patternsChanged()` might call back into `ExcludeSettings` to read
   the new state. If we held the write lock, that read deadlocks.

2. **Snapshot under read lock, iterate outside.** `shouldExclude` is
   the hot path (called per folder during a scan with hundreds of
   thousands of folders). Holding the read lock for the entire loop
   means writers (rare user actions) wait for the whole scan. Snapshot
   the set under the lock, iterate the copy without it:

   ```cpp
   QSet<QString> snapshot;
   { QReadLocker locker(&m_lock); snapshot = m_enabledPatterns; }
   for (const QString &p : snapshot) { /* match against folderName */ }
   ```

---

## Search runs off the GUI thread (2026-07-21)

**Symptom.** Typing beach-balled the UI. `FolderSearchWorker` /
`FileSearchWorker` debounced with a `QTimer`, but the timeout slot called
`PathCacheManager::search()` / `FileCacheManager::search()` **synchronously on
the main thread**. Those do an O(n) two-pass scan over the arena (~2M folders,
up to ~5M files), blocking the GUI thread for tens of ms per keystroke.

**Fix.** The debounce and the `resultsReady` / `searchFinished` signals stay on
the main thread; only the `search()` call moves to a background thread:

- `performSearch()` (main thread) snapshots the query, root path and
  `includeHidden` into locals, bumps a `m_generation` counter, then dispatches
  the scan via `QtConcurrent::run`. The lambda calls the static
  `computeResults(query, rootPath, includeHidden)` — which reads **no** member
  state, so nothing the GUI thread mutates is touched off-thread. The caches'
  `search()` is already thread-safe (internal `QReadWriteLock`).
- The background lambda marshals its result list back with
  `QMetaObject::invokeMethod(this, …, Qt::QueuedConnection)`. That delivery
  lambda runs on the main thread, so `m_generation` is only ever read/written
  there — no lock needed.

**Superseding stale results.** Each `performSearch()`/`cancel()` increments
`m_generation` (GUI thread only). When a background search finishes, the
delivery lambda compares its captured generation to the current one; if the
user has typed again (`gen != m_generation`) the results are dropped, so an
older, slower search can never overwrite a newer one on screen. `cancel()` also
bumps the counter, so a superseded in-flight search is silently discarded.

**Destruction safety.** The background lambda captures `this`. Workers normally
outlive the app, but stack-allocated workers in tests can be destroyed with a
task in flight, so the destructor waits on the stored `QFuture` before
returning. `waitForFinished()` doesn't block on the GUI event loop (the lambda
only *posts* a queued event and returns), so there's no deadlock; Qt drops the
still-queued delivery event when `~QObject` runs.

---

## QFileSystemModel + background thread

`QFileSystemModel` ships with its own internal `QFileInfoGatherer` thread
that walks the filesystem behind a tree view. We hit two consequences:

1. **Teardown race.** If the model is destroyed while the gatherer still
   has events queued for the tree view, the view dereferences a dead
   model. Fixed by `m_folderTreeView->setModel(nullptr)` in the dialog's
   destructor. This is straight out of `qt-reference/041_memory_management.md`.

2. **Test fragility.** Tests that create many dialogs in a row see
   intermittent SIGSEGV at offscreen-platform teardown. Mitigations:
   - `QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete)`
     between tests.
   - For arrow-key forwarding tests, target the deterministic
     `QListWidget` search-results list instead of the model-backed tree.

---

## Test infrastructure choices that follow from the rules

- **Run tests on `QT_QPA_PLATFORM=offscreen`** (set by `test_main.cpp`).
  The cocoa platform gives unreliable focus when the test process isn't
  the foreground app, which broke `QTest::keyClick` routing. Offscreen
  is deterministic.

- **Drive keystrokes through `QApplication::focusWidget()`, not the
  dialog**. `QTest::keyClick(&dialog, ...)` always delivers to the
  dialog, bypassing focus. Real users' keystrokes route through the
  OS to the focused widget. Test helper `typeString()` in
  `tests/UserInteractionTest.cpp` mirrors this.

- **Never test a `QMenu` via `QMenu::exec()` in a unit test.** It's
  blocking and modal. Either test the underlying state mutation
  directly (`setDefaultFavorite()`, `removeFavorite()`) or schedule a
  dismissal via `QTimer::singleShot`.

- **`stopScan()` between tests** in `UserInteractionTest::init()`. The
  `PathCacheManager` singleton outlives the per-test dialog; if a scan
  is still running when the next test starts, you can hit the
  cross-thread race even with the lock (deadlock-free but the test
  setup gets racy).

---

## Further reading

- [Qt docs — Threads and QObjects](https://doc.qt.io/qt-6/threads-qobject.html)
  — canonical Qt threading rules.
- [Qt docs — QReadWriteLock](https://doc.qt.io/qt-6/qreadwritelock.html)
  — the lock type we use.
- [qt-reference/041_memory_management.md](qt-reference/041_memory_management.md)
  — upstream's accumulated learnings, including the QFileSystemModel
  thread issue.
- [qt-reference/042_crash_prevention.md](qt-reference/042_crash_prevention.md)
  — upstream's crash pattern catalogue.
