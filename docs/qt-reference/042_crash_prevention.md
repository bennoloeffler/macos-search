# Crash Fix Review - Animation Safety

**Date:** 2026-02-05  
**Issue:** Segmentation fault on application startup  
**Status:** Fixed

---

## Problem Analysis

The application was crashing with a segmentation fault immediately on startup, before any dialogs were shown. This suggested the issue was not in dialog `showEvent()` handlers, but potentially in:

1. Widget initialization order
2. Graphics effects being applied before widgets are ready
3. Event handling race conditions

---

## Fixes Applied

### 1. Fixed `closeEvent` - Use-After-Free Bug
**Problem:** Capturing `QCloseEvent*` pointer in lambda - the event object is stack-allocated and destroyed when `closeEvent()` returns.

**Fix:** Removed event capture, call `accept()` directly after animation completes.

**Files Fixed:**
- All 9 dialogs with `closeEvent` overrides

### 2. Fixed `showEvent` - Initialization Order
**Problem:** Setting graphics effects after calling base `showEvent()` could cause issues if widget wasn't fully ready.

**Fix:** Set up opacity effect BEFORE calling base `showEvent()`, so widget starts invisible, then animate to visible.

**Pattern:**
```cpp
void Dialog::showEvent(QShowEvent *event)
{
    // Set up opacity effect BEFORE showing (so widget starts invisible)
    QGraphicsOpacityEffect *effect = qobject_cast<QGraphicsOpacityEffect*>(graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(effect);
    }
    effect->setOpacity(0.0);
    
    QDialog::showEvent(event); // Now show the widget (it will be invisible due to opacity 0)
    
    // Animate fade-in after widget is shown
    auto *anim = new QPropertyAnimation(effect, "opacity", this);
    anim->setDuration(200);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}
```

**Files Fixed:**
- `PluginsDialog.cpp`
- `MCPServersDialog.cpp`
- `BashToolsDialog.cpp`
- `APIWrappersDialog.cpp`
- `ConfigExplorerDialog.cpp`
- `PluginDetailDialog.cpp`
- `AddAPIDialog.cpp`
- `AddMarketplaceDialog.cpp`
- `MCPServerInstallDialog.cpp`

---

## Safety Checks Added

1. **Check for existing effects** - Reuse existing `QGraphicsOpacityEffect` if present
2. **Proper initialization order** - Set opacity BEFORE showing widget
3. **No event pointer capture** - Avoid use-after-free in lambdas
4. **Animation cleanup** - Use `DeleteWhenStopped` to prevent leaks

---

## Testing Recommendations

1. **Startup Test:** Verify application starts without crash
2. **Dialog Open Test:** Open each dialog and verify fade-in animation works
3. **Dialog Close Test:** Close each dialog and verify fade-out animation works
4. **Rapid Open/Close:** Rapidly open and close dialogs to test for race conditions
5. **Memory Leak Test:** Use valgrind or similar to check for leaks

---

## Remaining Considerations

If crashes persist, investigate:

1. **Widget Hierarchy:** Ensure widgets have proper parent before setting effects
2. **Thread Safety:** Verify all graphics operations happen on main thread
3. **Service Initialization:** Check if singleton services are initialized before use
4. **Signal/Slot Connections:** Verify connections are made after objects are fully constructed

---

## Config Button Crash (Segmentation fault when pressing Config)

**Date:** 2026-02-06  
**Issue:** Segmentation fault when user clicks the Config sidebar button.  
**Status:** Fixed

### Root cause

- **Reload during construction:** `ConfigExplorerDialog` called `m_effectiveSettings->reload()` in the constructor. That emitted `settingsChanged`, which triggered `refreshMCPServersTab()` and `refreshPluginsTab()` while the dialog was still being built. If any layout or content widget was not yet ready, or if tab switching ran during setup, it could cause a null dereference or re-entrancy crash.
- **Missing guards:** Refresh methods and the `settingsChanged` slot did not guard against null content widgets or layout.
- **Tab switch re-entrancy:** `animateTabSwitch` could be invoked with the same index (e.g. from `setCurrentIndex(0)` in `setupUi` after the connection was made), and did not guard against no-op/re-entrancy.

### Fixes applied

1. **Deferred initial reload:** Replaced direct `m_effectiveSettings->reload()` in the constructor with `QTimer::singleShot(0, ...)` so the first reload runs on the next event loop iteration, after the dialog is fully constructed and shown.
2. **Guarded signal slot:** The lambda connected to `settingsChanged` now checks `m_mcpServersContent && m_pluginsContent` before calling refresh methods.
3. **Null checks in refresh:** `refreshMCPServersTab()` and `refreshPluginsTab()` return early with a warning log if their content widget or layout is null. `onConfigFileChanged` only calls refresh when the corresponding content widget exists.
4. **Tab switch guard:** `animateTabSwitch()` only calls `setCurrentIndex(newIndex)` when `m_tabWidget->currentIndex() != newIndex` to avoid redundant/re-entrant updates.
5. **Logging:** `MaudeLogger::info` in `ConfigExplorerDialog` constructor and `showEvent`, and in `MainWindow` when the Config menu item is clicked and when the dialog is created/shown. Warning logs when refresh is skipped due to null state.

### Greybox test

- **Group:** `config-dialog`
- **Test:** `ConfigDialogOpensAndShows` — creates `ConfigExplorerDialog`, shows it, processes events, verifies dialog is visible and has a tab widget with at least 4 tabs.
- **Test:** `ConfigDialogAllTabsAndClose` — opens dialog, switches to each tab (MCP Servers, Plugins, Help, back to Project Config), clicks Close, verifies no crash.
- **Run:** `MAUDE_DEBUG=true ./build/maude-cp-v3.app/Contents/MacOS/maude-cp-v3 --run-test config-dialog` (or from Debug menu: Run Tests > config-dialog).

### Spacer double-delete (crash “after a blink”)

- **Cause:** In `refreshMCPServersTab` and `refreshPluginsTab`, the code did `delete spacer; delete item;` for layout items. For a stretch, the pointer returned by `takeAt()` is a `QSpacerItem*`, which *is* the layout item. Deleting the spacer and then the item double-freed the same object and caused a segmentation fault when the deferred refresh ran.
- **Fix:** For widget items: `delete item` only (widget is `deleteLater()`’d). For spacer items: `delete item` only (the item is the spacer — single delete).

---

## FolderBrowserDialog Crash (Choose / Cancel button SIGSEGV)

**Date:** 2026-02-08
**Issue:** Segmentation fault when clicking Choose or Cancel in the Select Folder dialog.
**Status:** Fixed

### Root cause

**Qt does NOT guarantee child widget destruction order.** When a parent QObject is destroyed, its children are deleted, but the order is **not guaranteed** to be reverse-creation-order for heap-allocated objects ([Qt docs: Object Trees & Ownership](https://doc.qt.io/qt-6/objecttrees.html)).

In `PathSelectorPopup`, the constructor stores a raw `QWidget *m_anchor` pointer to a sibling widget (`m_lineEdit`). Both are children of the same `PathSelectorUI` parent. During destruction:

1. Qt destroys `PathSelectorUI`'s children in unspecified order
2. `m_lineEdit` happens to be destroyed BEFORE `m_popup`
3. `PathSelectorPopup::~PathSelectorPopup()` calls `m_anchor->removeEventFilter(this)`
4. `m_anchor` is a **dangling pointer** → SIGSEGV at `QObject::removeEventFilter()`

The crash address was `0x0` or `0x50` — a null/low-offset dereference inside Qt's event filter list, confirming use-after-free on the anchor widget.

### Fixes applied

1. **`PathSelectorPopup.h`:** Changed `QWidget *m_anchor` to `QPointer<QWidget> m_anchor`. QPointer auto-nulls when the referenced QObject is destroyed, making the existing null check in the destructor effective.

2. **`FolderBrowserDialog.cpp`:** Added explicit destructor that:
   - Disconnects from `PathCacheManager` singleton (outlives dialog, has background threads)
   - Cancels pending search worker (debounce timer could fire during destruction)
   - Detaches `QFileSystemModel` from `QTreeView` before destruction (model may be freed before tree view)

3. **`FolderBrowserDialogTest.cpp`:** Removed QSKIP for offscreen mode — all 18 tests now pass.

### Stack trace (before fix)

```
frame #0: QArrayDataPointer<QPointer<QObject>>::reallocateAndGrow
frame #1: QList<QPointer<QObject>>::begin
frame #2: QObject::removeEventFilter(QObject*)
frame #3: PathSelectorPopup::~PathSelectorPopup() at PathSelectorPopup.cpp:30
frame #6: QObjectPrivate::deleteChildren()
frame #7: QWidget::~QWidget()
frame #8: PathSelectorUI::~PathSelectorUI()
```

### Rule for future development

**Never store raw pointers to sibling QObjects.** Use `QPointer<T>` for any pointer to a QObject you don't own, especially siblings (children of the same parent). Qt's child destruction order is NOT guaranteed.

---

**Last Updated:** 2026-02-08
