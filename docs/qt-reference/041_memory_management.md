# Qt6 Memory Management Guide

**For the Maude project — Crash prevention, ownership patterns, and destructor safety.**

Last updated: 2026-02-08

---

## Table of Contents

1. [The Golden Rules](#1-the-golden-rules)
2. [Parent-Child Ownership](#2-parent-child-ownership)
3. [Destruction Order](#3-destruction-order)
4. [Smart Pointers](#4-smart-pointers)
5. [deleteLater() vs delete](#5-deletelater-vs-delete)
6. [Signal/Slot Connection Lifetime](#6-signalslot-connection-lifetime)
7. [Event Filters](#7-event-filters)
8. [Destructor Best Practices](#8-destructor-best-practices)
9. [Common Crash Patterns — Q&A](#9-common-crash-patterns--qa)
10. [Maude Project Lessons Learned](#10-maude-project-lessons-learned)
11. [Quick Reference Checklist](#11-quick-reference-checklist)
12. [Sources](#12-sources)

---

## 1. The Golden Rules

These rules prevent the vast majority of Qt memory crashes:

| # | Rule | Why |
|---|------|-----|
| 1 | **Give every QObject a parent** (except top-level objects) | Parent auto-deletes children — no leaks, no manual cleanup |
| 2 | **Never store raw pointers to sibling QObjects** | Siblings can be destroyed in any order — use `QPointer<T>` |
| 3 | **Use `deleteLater()` instead of `delete`** for QObjects | Safe during event handling, cross-thread, and in slots |
| 4 | **Never mix stack allocation with parent-child** | Qt calls `delete` on children — double-free if child is on stack |
| 5 | **Always provide a context object for lambda connections** | Without context, lambda connections leak (never auto-disconnect) |
| 6 | **Disconnect from singletons in your destructor** | Singletons outlive dialogs — queued signals hit dead objects |

---

## 2. Parent-Child Ownership

### How It Works

When a QObject is created with a parent, it is added to the parent's `children()` list. The parent **takes ownership** and will call `delete` on every child in its destructor.

```cpp
// Parent owns the button — button is auto-deleted when parent is deleted
QPushButton *btn = new QPushButton("OK", parentWidget);
```

No manual `delete` needed. No smart pointer needed. The parent handles it.

### What Happens on Destruction

When a QObject is destroyed:

1. It emits `destroyed(QObject *obj)` — QPointers are notified here
2. All signals to/from the object are disconnected
3. All child objects are deleted (order **not guaranteed** for heap objects)
4. The object removes itself from its parent's children list
5. Pending posted events for the object are removed from the event queue

### QWidget Extension

QWidget extends parent-child with visual semantics:
- Child widgets are displayed inside the parent's coordinate system
- Child widgets are clipped to the parent's boundaries
- Hiding the parent hides all children

### What NOT to Do

```cpp
// DON'T: delete a child manually then let the parent try again
QPushButton *btn = new QPushButton("OK", parent);
delete btn;       // Removes from parent — OK, but unnecessary
delete parent;    // Would have deleted btn anyway

// DON'T: hold raw pointers to objects you don't own
class MyWidget : public QWidget {
    QLabel *m_statusLabel;  // DANGEROUS if label is a child of another widget
};

// DO: use QPointer for safety
class MyWidget : public QWidget {
    QPointer<QLabel> m_statusLabel;  // Auto-nulls if label is destroyed
};
```

---

## 3. Destruction Order

### Heap-Allocated Children (the dangerous case)

**Qt does NOT guarantee destruction order for heap-allocated children.**

When a parent is destroyed, it iterates its `children()` list and deletes each child. But the order of iteration is **not specified** and can vary between Qt versions, platforms, and even runs.

This means if Widget A and Widget B are both children of the same parent, **you cannot assume A is destroyed before B or vice versa**.

### Stack-Allocated Children (predictable, but risky)

C++ guarantees that local variables are destroyed in **reverse order of construction**. This makes stack allocation predictable:

```cpp
// SAFE: Parent constructed first, destroyed last
QWidget window;
QPushButton quit("Quit", &window);
// Destruction: ~quit() first (removes itself from parent), then ~window()
```

```cpp
// CRASH: Child constructed first, parent tries to delete it
QPushButton quit("Quit");      // Constructed first
QWidget window;                // Constructed second
quit.setParent(&window);
// Destruction: ~window() deletes quit (free), then ~quit() runs (double-free!)
```

**Rule: If you must use stack allocation with parents, always construct the parent first.**

### The Practical Solution

For heap-allocated widgets (which is 99% of Qt GUI code), **never assume destruction order between siblings**. If widget A needs to reference widget B and they share a parent:

```cpp
// BAD
QWidget *m_sibling;  // Might be a dangling pointer during destruction

// GOOD
QPointer<QWidget> m_sibling;  // Auto-nulls when sibling is destroyed
```

---

## 4. Smart Pointers

### QPointer<T> — Guarded Weak Reference

**Purpose:** Holds a pointer to a QObject that **auto-nulls** when the object is destroyed.

**When to use:**
- Storing a reference to a QObject you don't own
- References to sibling widgets (same parent)
- Modal dialog pointers that might be destroyed externally
- Any long-lived reference where the target could be deleted

```cpp
QPointer<QLabel> label = new QLabel("Hello", parent);

// Later...
if (label) {  // Returns false if label was destroyed
    label->setText("World");
}
```

**Key facts:**
- Only works with QObject subclasses
- Does NOT manage lifetime — it's an observer, not an owner
- Cleared by ~QObject, not ~QWidget (important: children may already be gone)
- Zero overhead for function parameters — just use raw pointers for params

**Qt 5+ behavior change:** QPointer is cleared by `~QObject()`, which runs **after** `~QWidget()` has already destroyed children. This means a QPointer to a parent widget may still appear valid while its children are being destroyed.

### QScopedPointer<T> — Exclusive RAII Ownership

**Purpose:** Deletes the object when the pointer goes out of scope. Single owner, non-copyable.

**When to use:**
- `Ui::*` form pointers
- Temporary heap allocations that need guaranteed cleanup
- Members that are NOT QObject children

```cpp
class MyDialog : public QDialog {
    QScopedPointer<Ui::MyDialog> ui;  // Auto-deleted in destructor

public:
    MyDialog(QWidget *parent) : QDialog(parent), ui(new Ui::MyDialog) {
        ui->setupUi(this);
    }
    // No destructor needed — QScopedPointer handles it
};
```

**Custom deleters:**
- `QScopedPointerDeleter<T>` — calls `delete` (default)
- `QScopedPointerArrayDeleter<T>` — calls `delete[]`
- `QScopedPointerPodDeleter` — calls `free()`
- `QScopedPointerDeleteLater` — calls `deleteLater()` (for QObjects in event loops)

### QSharedPointer<T> — Reference-Counted Shared Ownership

**Purpose:** Multiple owners share a pointer. Object deleted when last reference dies.

**When to use:**
- Shared data models across unrelated components
- Non-QObject types that need shared ownership
- Cache entries that may be referenced from multiple places

```cpp
QSharedPointer<DataModel> model = QSharedPointer<DataModel>::create();
QSharedPointer<DataModel> copy = model;  // Both point to same object
model.clear();  // copy still holds the object
copy.clear();   // Object is now deleted
```

**Warning: Do NOT mix QSharedPointer with QObject parent-child ownership.** The parent will try to `delete` the child, but QSharedPointer also thinks it owns it — double-free crash.

### Comparison Table

| Pointer | Owns object? | Copyable? | Auto-nulls? | Works with non-QObject? |
|---------|-------------|-----------|-------------|------------------------|
| Raw `T*` | No | Yes | No | Yes |
| `QPointer<T>` | No | Yes | Yes | No (QObject only) |
| `QScopedPointer<T>` | Yes (exclusive) | No | N/A | Yes |
| `QSharedPointer<T>` | Yes (shared) | Yes | N/A | Yes |
| `QWeakPointer<T>` | No | Yes | Yes | Yes (use with QSharedPointer) |

---

## 5. deleteLater() vs delete

### When `delete` Crashes

Direct `delete` on a QObject can crash if:

1. **The object is handling an event** — event processing continues on freed memory
2. **The object is in a different thread** — thread-safety violation
3. **You're inside a slot triggered by the object** — the caller still holds a reference
4. **Pending events exist** — event loop delivers events to freed memory

### How deleteLater() Works

```cpp
myObject->deleteLater();
```

1. Posts a `QEvent::DeferredDelete` to the event loop
2. Object continues to exist for the rest of the current event loop iteration
3. When control returns to the event loop, the object is deleted
4. All pending events for the object are delivered first

### When to Use Each

| Scenario | Use `delete` | Use `deleteLater()` |
|----------|-------------|-------------------|
| In a destructor cleaning up owned resources | Yes | No (event loop may not run) |
| Inside a slot connected to the object | No | Yes |
| Cross-thread deletion | No | Yes (thread-safe) |
| During event handling | No | Yes |
| Simple owned member, no event loop involvement | Yes | Either |
| Object with `QGraphicsEffect` applied | No | Yes |

### The Safe Default

**When in doubt, use `deleteLater()`.** It is always at least as safe as `delete`, and often safer. The only exception is when you know the event loop won't run (e.g., after `QCoreApplication::exit()`).

### Important Caveats

- `deleteLater()` inside a nested event loop (e.g., `QDialog::exec()`) won't execute until the **outer** event loop resumes
- If called after `QCoreApplication` is destroyed, the object is **never deleted** (memory leak, not crash)
- Multiple calls to `deleteLater()` are safe — object is only deleted once

---

## 6. Signal/Slot Connection Lifetime

### Automatic Disconnection

Qt automatically disconnects all signal-slot connections when **either** the sender or receiver is destroyed. This happens in `~QObject()`.

```cpp
connect(sender, &Sender::valueChanged, receiver, &Receiver::onValueChanged);
// If sender or receiver is deleted, the connection is removed — no crash
```

### Lambda Connections Need a Context Object

Lambda connections **without** a context object are **never** auto-disconnected:

```cpp
// DANGEROUS: Lambda outlives 'this' — crash when 'this' is deleted
connect(timer, &QTimer::timeout, [this]() {
    this->update();  // 'this' might be dead!
});

// SAFE: Connection auto-disconnected when 'this' is destroyed
connect(timer, &QTimer::timeout, this, [this]() {
    this->update();
});
```

**Always pass `this` (or another QObject) as the context parameter for lambda connections.**

### Destruction Timing Subtlety

Signals may still be delivered **during** object destruction, after the derived class destructor has run but before `~QObject()` disconnects everything.

```cpp
class MyWidget : public QWidget {
    ~MyWidget() {
        // At this point, MyWidget members are destroyed
        // But ~QObject hasn't run yet, so connections still exist
        // A signal could arrive here and call a slot that accesses destroyed members
    }
};
```

**Fix:** Disconnect explicitly at the top of your destructor if this is a concern:

```cpp
MyWidget::~MyWidget() {
    disconnect();  // Disconnect all connections immediately
    // Now safe to destroy members
}
```

### Singleton Connections Are Especially Dangerous

Singletons outlive dialogs. If a dialog connects to a singleton's signal, the singleton may emit after the dialog is destroyed:

```cpp
// In dialog constructor:
connect(PathCacheManager::instance(), &PathCacheManager::scanComplete,
        this, &MyDialog::onScanComplete);

// In dialog destructor — MUST disconnect:
MyDialog::~MyDialog() {
    disconnect(PathCacheManager::instance(), nullptr, this, nullptr);
}
```

For `Qt::QueuedConnection` (cross-thread signals), the signal is posted to the event queue. If the dialog is destroyed before the event is processed, Qt delivers the event to freed memory.

---

## 7. Event Filters

### Automatic Cleanup

When a QObject is destroyed, all event filters installed **on** it are automatically removed. You generally don't need to manually call `removeEventFilter()` in destructors.

### The Dangling Anchor Problem

However, if your event filter object stores a pointer to the watched object (common pattern), that pointer can dangle:

```cpp
class MyFilter : public QObject {
    QWidget *m_target;  // RAW POINTER — dangerous!

    ~MyFilter() {
        m_target->removeEventFilter(this);  // CRASH if m_target is already dead
    }
};
```

**Fix:** Use QPointer:

```cpp
class MyFilter : public QObject {
    QPointer<QWidget> m_target;

    ~MyFilter() {
        if (m_target) {
            m_target->removeEventFilter(this);
        }
    }
};
```

### Critical Rule for eventFilter()

If you delete the receiver object inside `eventFilter()`, you **must** return `true`. Returning `false` causes Qt to forward the event to the deleted object — crash.

```cpp
bool MyFilter::eventFilter(QObject *watched, QEvent *event) {
    if (shouldDelete) {
        watched->deleteLater();
        return true;  // MUST return true — object is being deleted
    }
    return false;
}
```

---

## 8. Destructor Best Practices

### When to Write a Custom Destructor

You need a custom destructor when:
- You hold references to objects you don't own (disconnect from singletons)
- You have background threads or workers that need cancellation
- You use QFileSystemModel (detach from view before destruction)
- You have non-QObject heap allocations (that QScopedPointer doesn't cover)

### Destructor Template

```cpp
MyDialog::~MyDialog()
{
    // 1. Disconnect from singletons and long-lived objects
    disconnect(MySingleton::instance(), nullptr, this, nullptr);

    // 2. Cancel background workers
    if (m_worker) {
        m_worker->cancel();
    }

    // 3. Detach models from views (model may be destroyed before view)
    if (m_treeView) {
        m_treeView->setModel(nullptr);
    }

    // 4. Stop timers (usually automatic, but explicit is safer)
    // m_debounceTimer->stop();

    // 5. DO NOT manually delete child widgets — parent handles that
    // 6. DO NOT call removeEventFilter — automatic on destruction
    // 7. Signals/slots auto-disconnect in ~QObject
}
```

### When NOT to Write a Destructor

If all your members are:
- Child QObjects (auto-deleted by parent)
- QScopedPointer members (auto-deleted)
- QPointer members (just nulled, not deleted)
- Stack-allocated value types (auto-destroyed)

Then `= default` is fine.

---

## 9. Common Crash Patterns — Q&A

### Q: My app crashes on exit. What's happening?

**A:** Usually one of:
1. **Widgets outlive QApplication** — all widgets must be destroyed before `~QCoreApplication`. Allocate your main window on the stack in `main()`.
2. **Static QObject lives too long** — singleton destroyed after QApplication. Use `QCoreApplication::aboutToQuit()` to clean up.
3. **Double deletion** — stack-allocated child with a parent. See [Destruction Order](#3-destruction-order).

### Q: My dialog crashes when I click a button. The button calls accept()/reject().

**A:** The crash is probably in the destructor, not the button click. When `accept()` or `reject()` is called:
1. The dialog is hidden
2. `exec()` returns
3. If the dialog is stack-allocated, the destructor runs immediately

Debug the destructor — look for dangling pointers, especially to sibling widgets.

### Q: I get SIGSEGV in QObject::removeEventFilter(). Why?

**A:** You're calling `removeEventFilter()` on a dangling pointer. The watched object was already destroyed before your filter object. Use `QPointer<QWidget>` for the anchor/target and check for null before calling `removeEventFilter()`.

### Q: My app crashes when I delete a widget with QGraphicsOpacityEffect.

**A:** Known Qt issue. The effect's paint area can extend beyond the widget's bounds in Qt's internal BSP tree. Solutions:
1. Call `setGraphicsEffect(nullptr)` before deleting the widget
2. Use `deleteLater()` instead of `delete`
3. Call `prepareGeometryChange()` before removal (for QGraphicsItems)

### Q: Cross-thread signal causes SIGSEGV.

**A:** The receiver was destroyed while a queued signal was in transit. Solutions:
1. Disconnect from the sender in the receiver's destructor
2. Use `QPointer` in the sender to check receiver validity
3. Use `Qt::QueuedConnection` with a context object

### Q: I called deleteLater() but the object is never deleted.

**A:** Check:
1. Is the event loop running? `deleteLater()` needs an active event loop.
2. Did you call it after `QCoreApplication::exit()`? Objects won't be deleted.
3. Are you in a nested event loop (e.g., `QDialog::exec()`)? Deletion waits for the **outer** loop.

### Q: QFileSystemModel crashes during dialog destruction.

**A:** QFileSystemModel uses a background thread (`QFileInfoGatherer`) that may still be running when the model is destroyed. If the model is destroyed before the tree view, the view may try to access the dead model. Fix: detach the model from the view in your destructor:

```cpp
MyDialog::~MyDialog() {
    m_treeView->setModel(nullptr);  // Detach before destruction
}
```

### Q: My QPointer appears valid but the widget's children are already destroyed.

**A:** QPointer is cleared by `~QObject()`, which runs **after** `~QWidget()`. By the time `~QWidget()` runs, all child widgets are already destroyed, but QPointers to the parent still appear valid. Be careful accessing children through a QPointer to a parent during destruction.

### Q: When should I call this->disconnect() in my destructor?

**A:** When:
- You're connected to singletons or long-lived objects
- You have `Qt::QueuedConnection` connections from other threads
- Your slots access members that are destroyed before `~QObject()` runs
- You see crashes in slots during destruction

```cpp
MyWidget::~MyWidget() {
    disconnect();  // Nuclear option: disconnect everything immediately
}
```

---

## 10. Maude Project Lessons Learned

### Crash #1: PathSelectorPopup — Sibling Dangling Pointer

**Symptom:** SIGSEGV when clicking Choose or Cancel in FolderBrowserDialog.

**Root cause:** `PathSelectorPopup` stored a raw `QWidget *m_anchor` pointing to sibling widget `m_lineEdit`. Both were children of `PathSelectorUI`. During destruction, `m_lineEdit` was freed before `m_popup`, so `m_anchor->removeEventFilter(this)` in the popup's destructor crashed on a dangling pointer.

**Fix:** Changed to `QPointer<QWidget> m_anchor`. The existing null check `if (m_anchor)` now works because QPointer auto-nulls.

**Stack trace:**
```
QObject::removeEventFilter(QObject*)
PathSelectorPopup::~PathSelectorPopup()
QObjectPrivate::deleteChildren()
QWidget::~QWidget()
PathSelectorUI::~PathSelectorUI()
```

### Crash #2: ConfigExplorerDialog — Reload During Construction

**Symptom:** SIGSEGV when clicking Config sidebar button.

**Root cause:** Constructor called `m_effectiveSettings->reload()` which emitted `settingsChanged`, triggering refresh methods while tabs were still being built. Null content widgets and re-entrant tab switching caused crashes.

**Fix:** Deferred initial reload with `QTimer::singleShot(0, ...)`, added null guards in refresh methods, and guarded `animateTabSwitch()` against same-index re-entrancy.

### Crash #3: Animation closeEvent — Use-After-Free

**Symptom:** Intermittent crash when closing dialogs with fade-out animation.

**Root cause:** `QCloseEvent*` was captured in a lambda. The event is stack-allocated and destroyed when `closeEvent()` returns. The animation's `finished` signal fired later, calling `event->accept()` on freed memory.

**Fix:** Removed event capture from lambda. Call `accept()` directly after animation completes, or use `QDialog::accept()`/`reject()` instead.

### Crash #4: Layout Spacer Double-Delete

**Symptom:** SIGSEGV "after a blink" when ConfigExplorerDialog refreshed tabs.

**Root cause:** Code did `delete spacer; delete item;` for layout stretch items. For spacers, the `QLayoutItem*` returned by `takeAt()` **is** the `QSpacerItem*` — same pointer. Deleting both double-freed.

**Fix:** For spacers, only `delete item` (the item is the spacer — one delete).

### Crash #5: FolderBrowserDialog — QFileSystemModel Background Thread

**Symptom:** Intermittent crash during dialog destruction.

**Root cause:** QFileSystemModel has an internal `QFileInfoGatherer` thread. If the tree view tries to access the model after the model is freed (destruction order not guaranteed), crash.

**Fix:** Explicit destructor detaches model from view: `m_treeView->setModel(nullptr)`.

---

## 11. Quick Reference Checklist

Use this checklist when writing new Qt widgets or reviewing existing ones:

### Construction
- [ ] All child widgets created with `this` as parent
- [ ] Parent constructed before children (if stack-allocated)
- [ ] No `reload()` or signal emission in constructor — defer with `QTimer::singleShot(0, ...)`
- [ ] Lambda connections use context object: `connect(sender, signal, this, lambda)`

### Member Pointers
- [ ] Sibling widget references use `QPointer<QWidget>`
- [ ] No raw pointers to objects you don't own
- [ ] UI pointer uses `QScopedPointer<Ui::MyWidget>` or is deleted in destructor

### Destruction
- [ ] Disconnect from singletons: `disconnect(Singleton::instance(), nullptr, this, nullptr)`
- [ ] Cancel background workers
- [ ] Detach models from views: `m_view->setModel(nullptr)`
- [ ] Do NOT manually delete child widgets
- [ ] Do NOT capture QCloseEvent* in lambdas

### Signals/Slots
- [ ] Cross-thread connections use `Qt::QueuedConnection`
- [ ] Lambda connections always have a context object
- [ ] Slots that delete objects return early or use `deleteLater()`
- [ ] `eventFilter()` returns `true` if it deletes the watched object

### Testing
- [ ] Test with `QT_QPA_PLATFORM=offscreen` for headless crashes
- [ ] Test rapid open/close of dialogs for race conditions
- [ ] Check for SIGSEGV during destruction (add fprintf debugging)
- [ ] Validate with lldb/gdb for stack traces on crashes

---

## 12. Sources

### Official Qt Documentation
- [Object Trees & Ownership](https://doc.qt.io/qt-6/objecttrees.html) — Parent-child lifecycle
- [QObject Class](https://doc.qt.io/qt-6/qobject.html) — Destruction, deleteLater(), event filters
- [QPointer Class](https://doc.qt.io/qt-6/qpointer.html) — Guarded pointers
- [QScopedPointer Class](https://doc.qt.io/qt-6/qscopedpointer.html) — RAII ownership
- [QSharedPointer Class](https://doc.qt.io/qt-6/qsharedpointer.html) — Shared ownership
- [Signals & Slots](https://doc.qt.io/qt-6/signalsandslots.html) — Connection lifetime
- [The Event System](https://doc.qt.io/qt-6/eventsandfilters.html) — Event filter lifecycle
- [QFileSystemModel](https://doc.qt.io/qt-6/qfilesystemmodel.html) — Background thread behavior

### Blog Posts
- [How to Crash Almost Every Qt/KDE Application — KDE Blogs](https://blogs.kde.org/2009/03/26/how-crash-almost-every-qtkde-application-and-how-fix-it-0/) — Modal dialog + QPointer pattern
- [deleteLater: Managing the QObject Lifecycle — SEP](https://sep.com/blog/deletelater-managing-the-qobject-lifecycle-in-c/) — deleteLater() deep dive
- [PSA: QPointer Has a Terrible Name — KDAB](https://www.kdab.com/psa-qpointer-has-a-terrible-name/) — QPointer is a weak pointer, not a smart pointer
- [Crash Course in Qt for C++ Developers, Part 4 — Clean Qt](https://www.cleanqt.io/blog/crash-course-in-qt-for-c++-developers,-part-4) — Memory management patterns

### Qt Forums & StackOverflow
- [Strange Crash with QObject Hierarchy Deletion](https://forum.qt.io/topic/96197/strange-crash-with-qobject-hierarchy-deletion)
- [Widget with Nested Layout Crashes in Destructor](https://forum.qt.io/topic/96952/widget-with-nested-layout-crashes-in-destructor)
- [QWidget Assumes Heap Allocation?](https://forum.qt.io/topic/8596/solved-qwidget-assumes-allocation-on-heap-not-stack)
- [Cross-Thread Signals, Disconnect and Destructors](https://forum.qt.io/topic/243/cross-thread-signals-disconnect-and-destructors)
- [Does QObject Distinguish Stack vs Heap Children?](https://forum.qt.io/topic/19997/solved-does-qobject-distinguish-between-stack-and-heap-allocated-children)
- [When Exactly Does deleteLater() Delete?](https://forum.qt.io/topic/94369/when-exactly-does-qobject-deletelater-actually-delete)
- [Delete vs deleteLater()](https://forum.qt.io/topic/91180/delete-vs-deletelater)
- [QApplication Destructor Crashes on Exit](https://forum.qt.io/topic/32091/qapplication-qapplication-destructor-crashes-segmentation-fault-when-exit-program)

### Qt Bug Reports
- [QTBUG-10181: QFileSystemModel Has No Destructor](https://bugreports.qt.io/browse/QTBUG-10181)
- [QTBUG-21715: Possible Crash in ~QPointer() Cross-Thread](https://bugreports.qt.io/browse/QTBUG-21715)
- [QTBUG-53103: Events Sent to Deleted Widget on Destruction](https://bugreports.qt.io/browse/QTBUG-53103)

### Project Documentation
- [042_crash_prevention.md](./042_crash_prevention.md) — Maude-specific crash analyses and fixes
