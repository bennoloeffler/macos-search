# Qt Widgets E2E Testing — Special Cases & Advanced Patterns

**Prerequisites:** Read `020_testing_strategy.md` first. This document covers situations where the basics aren't enough.

---

## When You Need This Document

| Situation | Section |
|-----------|---------|
| `findChild()` is too slow or unreliable | [Test API](#1-test-api-when-findchild-isnt-enough) |
| Dialogs appear dynamically, can't find them | [Widget Registry](#2-widget-registry-for-dynamic-dialogs) |
| Modal dialogs block your test | [Modal Dialog Testing](#3-modal-dialog-testing) |
| Need to wait for something without a signal | [Advanced Waiting](#4-advanced-waiting-patterns) |
| Terminal tests fail in offscreen mode | [Headless Edge Cases](#5-headless-edge-cases) |
| Need screenshots for debugging | [Screenshot Capture](#6-screenshot-capture) |
| Many tests, need to run subsets | [Test Filtering](#7-test-filtering--categories) |
| Tests depend on each other | [Test Dependencies](#8-test-dependencies) |
| Tests pass locally, fail in CI | [Troubleshooting](#9-troubleshooting) |

---

## 1. Test API (When findChild Isn't Enough)

### When to Use

- Tests are verbose with repeated `findChild()` calls
- Need centralized error handling
- Want screenshot capture integrated
- Building a larger test suite

### Implementation

```cpp
#ifdef ENABLE_TEST_API
class TestAPI : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE void click(const QString& name) {
        auto* w = qApp->findChild<QWidget*>(name);
        if (!w) {
            qWarning() << "Widget not found:" << name;
            listAllWidgets();  // Debug help
            Q_ASSERT(false);
        }
        QTest::mouseClick(w, Qt::LeftButton);
    }

    Q_INVOKABLE void type(const QString& name, const QString& text) {
        auto* w = qApp->findChild<QWidget*>(name);
        Q_ASSERT(w);
        w->setFocus();
        QTest::keyClicks(w, text);
    }

    Q_INVOKABLE void clear(const QString& name) {
        if (auto* edit = qApp->findChild<QLineEdit*>(name)) {
            edit->clear();
        }
    }

    Q_INVOKABLE QString text(const QString& name) {
        if (auto* lbl = qApp->findChild<QLabel*>(name)) return lbl->text();
        if (auto* edit = qApp->findChild<QLineEdit*>(name)) return edit->text();
        if (auto* btn = qApp->findChild<QPushButton*>(name)) return btn->text();
        return QString();
    }

    Q_INVOKABLE bool isEnabled(const QString& name) {
        auto* w = qApp->findChild<QWidget*>(name);
        return w && w->isEnabled();
    }

    Q_INVOKABLE void wait(int ms) { QTest::qWait(ms); }

    Q_INVOKABLE void capture(const QString& name) {
        QString dir = qEnvironmentVariable("E2E_SCREENSHOT_DIR", "./screenshots");
        QDir().mkpath(dir);
        for (auto* w : qApp->topLevelWidgets()) {
            if (w->isVisible()) {
                w->grab().save(QString("%1/%2.png").arg(dir, name));
                break;
            }
        }
    }

private:
    void listAllWidgets() {
        qDebug() << "=== All widgets ===";
        for (auto* w : qApp->allWidgets()) {
            if (!w->objectName().isEmpty()) {
                qDebug() << w->objectName() << w->metaObject()->className();
            }
        }
    }
};
#endif
```

### Register at Startup

```cpp
// main.cpp
#ifdef ENABLE_TEST_API
#include "testapi.h"
#endif

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    #ifdef ENABLE_TEST_API
    app.setProperty("testApi", QVariant::fromValue(new TestAPI(&app)));
    #endif

    // ... rest of app
}
```

### CMake Integration

```cmake
option(ENABLE_TEST_API "Enable test API (debug only)" OFF)

if(ENABLE_TEST_API OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(MyApp PRIVATE ENABLE_TEST_API)
endif()
```

---

## 2. Widget Registry (For Dynamic Dialogs)

### The Problem

Dialogs created on-demand can't be found by `findChild()` because they don't exist yet or aren't in the widget hierarchy.

### Solution: Self-Registering Widgets

```cpp
// widgetregistry.h
class WidgetRegistry {
public:
    static WidgetRegistry* instance() {
        static WidgetRegistry inst;
        return &inst;
    }

    void reg(const QString& name, QWidget* w) { m_widgets[name] = w; }
    void unreg(const QString& name) { m_widgets.remove(name); }

    template<typename T>
    T* find(const QString& name) {
        return qobject_cast<T*>(m_widgets.value(name));
    }

    QWidget* find(const QString& name) {
        return m_widgets.value(name);
    }

private:
    QHash<QString, QWidget*> m_widgets;
};

#define REGISTER_WIDGET(name) WidgetRegistry::instance()->reg(name, this)
#define UNREGISTER_WIDGET(name) WidgetRegistry::instance()->unreg(name)
```

### Usage in Dialogs

```cpp
// settingsdialog.cpp
SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("SettingsDialog");  // Still needed for findChild
    REGISTER_WIDGET("SettingsDialog"); // Also register for instant lookup
    setupUi();
}

SettingsDialog::~SettingsDialog() {
    UNREGISTER_WIDGET("SettingsDialog");
}
```

### Usage in Tests

```cpp
void test_settings() {
    // Trigger dialog open
    api->click("settingsButton");
    api->wait(100);

    // Instant lookup - no searching needed
    auto* dlg = WidgetRegistry::instance()->find<QDialog>("SettingsDialog");
    QVERIFY(dlg);
    QVERIFY(dlg->isVisible());

    // Interact
    api->type("fontSizeEdit", "16");
    api->click("okButton");
}
```

---

## 3. Modal Dialog Testing

### The Problem

```cpp
dialog->exec();  // BLOCKS - test code after this won't run
```

### Solution: Schedule Actions Before Modal Opens

```cpp
void test_delete_confirmation() {
    // Schedule what to do WHEN dialog appears
    QTimer::singleShot(200, [=]() {
        auto* dlg = WidgetRegistry::instance()->find<QDialog>("ConfirmDialog");
        if (dlg) {
            auto* yes = dlg->findChild<QPushButton*>("yesButton");
            if (yes) QTest::mouseClick(yes, Qt::LeftButton);
        }
    });

    // NOW trigger the modal (this blocks, but timer runs)
    api->click("deleteButton");

    // After modal closes, verify result
    api->wait(100);
    QCOMPARE(api->text("statusLabel"), "Deleted");
}
```

### Alternative: Make Dialogs Non-Modal in Tests

```cpp
// In dialog code
void ConfirmDialog::show() {
    #ifdef ENABLE_TEST_API
    QDialog::show();  // Non-blocking
    #else
    QDialog::exec();  // Blocking (production)
    #endif
}
```

### Native Dialogs: Avoid Them

```cpp
// ❌ These don't work in headless/tests
QFileDialog::getOpenFileName();
QMessageBox::question();
QColorDialog::getColor();

// ✅ Use non-native variants
QFileDialog dlg(this);
dlg.setOption(QFileDialog::DontUseNativeDialog, true);
dlg.exec();
```

---

## 4. Advanced Waiting Patterns

### When QSignalSpy Works (Prefer This)

```cpp
// Object emits a signal when done
QSignalSpy spy(loader, &DataLoader::finished);
loader->loadAsync();
QVERIFY(spy.wait(5000));  // Blocks until signal or timeout
```

### When No Signal Exists: Polling

```cpp
// Helper function
bool waitForCondition(std::function<bool()> condition, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QTest::qWait(50);
        QCoreApplication::processEvents();
    }
    return condition();
}

// Usage
QVERIFY(waitForCondition([&]() {
    return statusLabel->text() == "Ready";
}, 5000));
```

### When to Just Use qWait

| Scenario | Use qWait? |
|----------|------------|
| External process (Claude) | ✅ Yes |
| Animation completion | ✅ Yes (if no signal) |
| Network request | ❌ Use QSignalSpy |
| File operation | ❌ Use QSignalSpy |
| Internal async | ❌ Use QSignalSpy |

---

## 5. Headless Edge Cases

### Basic Rule (From 020_testing_strategy.md)

```bash
QT_QPA_PLATFORM=offscreen ./tests
```

### When Offscreen Fails

| Problem | Solution |
|---------|----------|
| Terminal/PTY tests fail | Use Xvfb instead |
| OpenGL required | Use Xvfb instead |
| Native dialogs | Avoid them (see Section 3) |
| Drag & drop | Use Xvfb instead |

### Xvfb (Virtual Framebuffer)

```bash
# Linux only - provides virtual display
xvfb-run --auto-servernum ./tests

# With specific screen size
xvfb-run --auto-servernum --server-args="-screen 0 1920x1080x24" ./tests
```

### Platform-Specific CI

```yaml
jobs:
  test:
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
    runs-on: ${{ matrix.os }}

    steps:
      # ... build steps ...

      - name: Test (Linux)
        if: runner.os == 'Linux'
        run: xvfb-run ctest --test-dir build --output-on-failure

      - name: Test (macOS/Windows)
        if: runner.os != 'Linux'
        run: ctest --test-dir build --output-on-failure
        env:
          QT_QPA_PLATFORM: offscreen
```

---

## 6. Screenshot Capture

### When to Use

- Debugging test failures
- Visual verification (manual review)
- Documentation

### Implementation (Already in Test API)

```cpp
void capture(const QString& name) {
    QString dir = qEnvironmentVariable("E2E_SCREENSHOT_DIR", "./screenshots");
    QDir().mkpath(dir);

    for (auto* w : qApp->topLevelWidgets()) {
        if (w->isVisible()) {
            QString path = QString("%1/%2.png").arg(dir, name);
            w->grab().save(path);
            qDebug() << "Screenshot:" << path;
            break;
        }
    }
}
```

### Usage Pattern

```cpp
void test_settings_flow() {
    api->capture("01_initial");

    api->click("settingsButton");
    api->wait(200);
    api->capture("02_dialog_open");

    api->type("fontSizeEdit", "18");
    api->capture("03_font_changed");

    api->click("okButton");
    api->wait(100);
    api->capture("04_dialog_closed");
}
```

### CI: Upload Screenshots

```yaml
- name: Upload screenshots
  if: always()  # Even on failure
  uses: actions/upload-artifact@v4
  with:
    name: screenshots-${{ matrix.os }}
    path: build/screenshots/
    if-no-files-found: ignore
```

### Black Screenshots? Fix:

```cpp
w.show();
QTest::qWaitForWindowExposed(&w);
QCoreApplication::processEvents();  // Process paint events
QTest::qWait(100);  // Extra time for rendering
api->capture("now_it_works");
```

---

## 7. Test Filtering & Categories

### Environment Variable Filter

```bash
# Run only tests containing "Settings"
E2E_TEST_FILTER="Settings" ./tests

# Run only tests containing "Terminal"
E2E_TEST_FILTER="Terminal" ./tests
```

### Organizing Tests with Prefixes

```cpp
class E2ETests : public QObject {
    Q_OBJECT
private slots:
    // Critical path - must always pass
    void CRITICAL_app_starts();
    void CRITICAL_main_window_shows();

    // UI component tests
    void UI_settings_dialog_opens();
    void UI_settings_saves_values();

    // Terminal-specific
    void TERMINAL_sends_command();
    void TERMINAL_receives_output();

    // Visual verification (screenshots)
    void DRY_main_window_layout();
    void DRY_settings_dialog_layout();
};
```

### Running by Category

```bash
E2E_TEST_FILTER="CRITICAL" ./tests  # Just critical
E2E_TEST_FILTER="UI_" ./tests       # Just UI tests
E2E_TEST_FILTER="DRY" ./tests       # Just screenshot tests
```

---

## 8. Test Dependencies

### The Problem

Some tests depend on state from previous tests:

```cpp
void test_open_project() {
    // Opens a project, shows terminal
}

void test_terminal_command() {
    // Assumes project is open - FAILS if run alone
}
```

### Solutions

**Option A: Make tests self-contained**

```cpp
void test_terminal_command() {
    // Setup: ensure project is open
    if (!isProjectOpen()) {
        openTestProject();
        api->wait(2000);
    }

    // Now test
    terminal->sendText("echo hello");
    // ...
}
```

**Option B: Document dependencies**

```cpp
// Requires: test_open_project must run first
void test_terminal_command() { ... }
```

**Option C: Test groups**

```bash
# Run setup + dependent tests together
E2E_TEST_FILTER="CRITICAL" ./tests && E2E_TEST_FILTER="TERMINAL" ./tests
```

---

## 9. Troubleshooting

### Widget Not Found

```cpp
// Debug: print all widgets with names
for (auto* w : qApp->allWidgets()) {
    if (!w->objectName().isEmpty()) {
        qDebug() << w->objectName()
                 << w->metaObject()->className()
                 << (w->isVisible() ? "visible" : "hidden");
    }
}
```

**Common causes:**
- `setObjectName()` not called
- Typo in name
- Widget not created yet (add wait)
- Widget in wrong parent (use `qApp->findChild` not `parent->findChild`)

### Test Hangs

**Cause:** Modal dialog blocking event loop

**Fix:** Use `QTimer::singleShot()` pattern (Section 3)

### Test Passes Locally, Fails in CI

| Cause | Fix |
|-------|-----|
| Timing | Add waits or use signals |
| Display | Use offscreen or Xvfb |
| Font differences | Don't assert on visual properties |
| Path differences | Use `QStandardPaths` |

### Terminal Tests Fail in Offscreen

**Fix:** Use Xvfb instead:

```bash
xvfb-run ./tests
```

### Screenshots Are Black

**Fix:** Ensure widget is painted:

```cpp
widget->show();
QTest::qWaitForWindowExposed(widget);
QCoreApplication::processEvents();
QTest::qWait(100);
```

---

## Summary: When to Use What

| Need | Basic (020_testing_strategy.md) | Advanced (this doc) |
|------|---------------------------|---------------------|
| Simple widget test | `findChild()` + `QCOMPARE` | — |
| Many tests | — | Test API |
| Dynamic dialogs | — | Widget Registry |
| Modal dialogs | — | QTimer::singleShot |
| External process wait | `QTest::qWait()` | — |
| Internal async wait | `QSignalSpy` | Polling helper |
| Headless | `offscreen` | Xvfb for PTY |
| Debug failures | — | Screenshot capture |
| Large test suite | — | Test filtering |

**Start with the basics. Add complexity only when needed.**
