# Qt Widgets E2E Testing — Simplified

Minimal approach for cross-platform, headless UI testing with Qt 6 Widgets.

---

## The Only Rules

1. Every widget gets `setObjectName()`
2. Test state, not pixels
3. Run invisible with `QT_QPA_PLATFORM=offscreen`
4. Waits are OK for external processes

---

## 1. In Your Widget Code: Give Names

```cpp
// mainwindow.cpp - constructor or setupUi

submitButton = new QPushButton("Submit", this);
submitButton->setObjectName("submitButton");

emailEdit = new QLineEdit(this);
emailEdit->setObjectName("emailEdit");

statusLabel = new QLabel(this);
statusLabel->setObjectName("statusLabel");

terminal = new QTermWidget(0, this);
terminal->setObjectName("terminal");
```

**Rule:** Every widget you want to test needs `setObjectName()`.

---

## 2. In Your Test: Find by Name

```cpp
#include <QtTest>
#include "mainwindow.h"

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void test_submit();
};

void MainWindowTest::test_submit() {
    MainWindow w;
    w.show();
    QTest::qWaitForWindowExposed(&w);

    // Find widgets by name
    auto *edit = w.findChild<QLineEdit*>("emailEdit");
    auto *btn = w.findChild<QPushButton*>("submitButton");
    auto *status = w.findChild<QLabel*>("statusLabel");

    QVERIFY(edit && btn && status);

    // Simulate user input
    QTest::keyClicks(edit, "test@test.com");
    QTest::mouseClick(btn, Qt::LeftButton);

    // Assert state (not geometry, not pixels)
    QCOMPARE(status->text(), "Success");
}

QTEST_MAIN(MainWindowTest)
#include "tst_mainwindow.moc"
```

---

## 3. Run Invisible (Headless)

```bash
# No windows appear — completely invisible
QT_QPA_PLATFORM=offscreen ./build/tests
```

Works on macOS, Linux, and Windows.

---

## 4. Waits Are OK for External Processes

For internal Qt signals, prefer `QSignalSpy`:

```cpp
QSignalSpy spy(worker, &Worker::finished);
QVERIFY(spy.wait(2000));
```

For external processes (like Claude Code), waits are fine:

```cpp
// Claude needs time to start — no signal available
QTest::qWait(5000);
```

---

## 5. Terminal Testing (QTermWidget)

Send text and submit with Enter:

```cpp
auto *term = w.findChild<QTermWidget*>("terminal");

// Type text
term->sendText("echo hello");

// Press Enter (sendText("\n") doesn't work!)
QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, "\r");
term->sendKeyEvent(&enter);
```

---

## 6. CMake Setup

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

# Main app
qt_add_executable(MyApp src/main.cpp src/mainwindow.cpp)
target_link_libraries(MyApp PRIVATE Qt6::Widgets)

# Tests
qt_add_executable(MyAppTests tests/tst_mainwindow.cpp)
target_link_libraries(MyAppTests PRIVATE Qt6::Widgets Qt6::Test)

enable_testing()
add_test(NAME MyAppTests COMMAND MyAppTests)

# Automatic headless mode for ctest
set_tests_properties(MyAppTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

With this setup, `ctest --test-dir build` runs headless automatically.

---

## 7. CI (GitHub Actions)

```yaml
- name: Test
  run: ctest --test-dir build --output-on-failure
```

**Note:** `ctest` uses the `QT_QPA_PLATFORM=offscreen` from `set_tests_properties` automatically.

Linux alternative (if offscreen has issues with PTY):

```yaml
- name: Test (Linux)
  run: xvfb-run ctest --test-dir build --output-on-failure
```

---

## Checklist

| Rule | Check |
|------|-------|
| All testable widgets have `setObjectName()` | ☐ |
| Tests use `findChild<>()` by name | ☐ |
| Assertions check state, not geometry | ☐ |
| Tests run with `QT_QPA_PLATFORM=offscreen` | ☐ |

---

## What NOT to Test

- Pixel positions
- Widget sizes
- Visual appearance
- Native OS dialogs

---

## Summary

```cpp
// Widget code
button->setObjectName("myButton");

// Test code
auto *btn = w.findChild<QPushButton*>("myButton");
QTest::mouseClick(btn, Qt::LeftButton);
QCOMPARE(label->text(), "Clicked");
```

```bash
# Run invisible
QT_QPA_PLATFORM=offscreen ./tests
```

Add complexity only when you hit real problems.
