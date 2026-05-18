# Implementation Learnings

Key lessons and patterns discovered during the Maude implementation.

---

## 1. Qt Application Bootstrap

### 1.1 macOS Style Setup

The macOS style must be set **before** `QApplication` is created:

```cpp
#ifdef Q_OS_MACOS
    QApplication::setStyle("macos");
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

QApplication app(argc, argv);
```

**Gotcha:** Setting style after `QApplication` construction may not work correctly.

### 1.2 Font Initialization

SF Pro Text may not be installed on all Macs. Always fall back to system font:

```cpp
#ifdef Q_OS_MACOS
    QFont appFont("SF Pro Text", 13);
    if (!QFontInfo(appFont).family().contains("SF Pro", Qt::CaseInsensitive)) {
        appFont = QApplication::font();
        appFont.setPointSize(13);
    }
#endif
QApplication::setFont(appFont);
```

### 1.3 Application Icons

Icon paths differ between development and bundle builds:

```cpp
#ifdef Q_OS_MACOS
    // Bundle: /path/to/maude-cp-v3.app/Contents/Resources/maude.icns
    QString iconPath = QCoreApplication::applicationDirPath() + "/../Resources/maude.icns";
    if (!QFile::exists(iconPath)) {
        // Development: /path/to/build/../../assets/maude.icns
        iconPath = QCoreApplication::applicationDirPath() + "/../../assets/maude.icns";
    }
#endif
```

---

## 2. Configuration Management

### 2.1 Isolated Config Directory

Maude uses `~/.maude/` to keep configuration separate from Claude CLI:

```cpp
QString MaudeConfig::configDir() {
    QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return homeDir + "/.maude";
}
```

The `CLAUDE_CONFIG_DIR` environment variable is set to `~/.maude/claude-config` to redirect Claude Code.

### 2.2 Settings Migration

When migrating from legacy QSettings, check for app-specific keys only:

```cpp
// macOS QSettings includes system NSGlobalDomain settings
// Cannot use allKeys().isEmpty() - must check specific keys
bool hasRecentProjects = legacySettings.contains("recentProjects");
```

**Gotcha:** On macOS, `QSettings::allKeys()` includes global system settings, making it useless for checking if app-specific settings exist.

### 2.3 QSettings Format

Use `.ini` format for portable, human-readable settings:

```cpp
QSettings newSettings(settingsFilePath(), QSettings::IniFormat);
```

---

## 3. Terminal Integration

### 3.1 QTermWidget Initialization

Create terminal with `startnow=0` to defer shell start:

```cpp
m_terminal = new QTermWidget(0, this);  // 0 = don't start shell immediately
```

This allows configuration before launching the shell.

### 3.2 Process Termination

Use SIGHUP + SIGTERM for graceful termination:

```cpp
void TerminalView::stopTerminalProcess() {
    int pid = shellPID();
    if (pid > 0) {
        ::kill(pid, SIGHUP);   // Standard terminal close
        ::kill(pid, SIGTERM);  // Backup for stubborn processes
    }
}
```

### 3.3 Copy/Paste: QTermWidget Focus Proxy Gotcha

**Problem:** `TerminalView::keyPressEvent()` never receives keyboard events.

QTermWidget sets its focus proxy to the internal `TerminalDisplay` widget:

```cpp
// Inside qtermwidget.cpp — QTermWidget constructor:
this->setFocusProxy(m_impl->m_terminalDisplay);
```

This means when the user types in the terminal, key events go directly to `TerminalDisplay::keyPressEvent()`, which emits `keyPressedSignal` and sends everything to the VT102 emulation. The parent `TerminalView::keyPressEvent()` is **never called** because `TerminalView` never has focus.

**Solution:** Install an event filter on the actual focused widget:

```cpp
// In TerminalView::setupUi():
QWidget *focusTarget = m_terminal->focusProxy();
if (focusTarget)
    focusTarget->installEventFilter(this);
else
    m_terminal->installEventFilter(this);
```

The event filter intercepts both `ShortcutOverride` (to prevent Qt from stealing the shortcut) and `KeyPress` (to perform the action):

```cpp
bool TerminalView::eventFilter(QObject *obj, QEvent *event)
{
    // Only handle KeyPress and ShortcutOverride
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)
        return QWidget::eventFilter(obj, event);

    auto *keyEvent = static_cast<QKeyEvent *>(event);

#ifdef Q_OS_MACOS
    const bool isCopyShortcut = (keyEvent->key() == Qt::Key_C &&
                                  keyEvent->modifiers() == Qt::MetaModifier);   // Cmd+C
    const bool isPasteShortcut = (keyEvent->key() == Qt::Key_V &&
                                   keyEvent->modifiers() == Qt::MetaModifier);  // Cmd+V
#else
    // Ctrl+Shift+C/V because Ctrl+C is SIGINT, Ctrl+V is literal-next in terminals
    const bool isCopyShortcut = (keyEvent->key() == Qt::Key_C &&
                                  keyEvent->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier));
    const bool isPasteShortcut = (keyEvent->key() == Qt::Key_V &&
                                   keyEvent->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier));
#endif

    if (!isCopyShortcut && !isPasteShortcut)
        return QWidget::eventFilter(obj, event);

    // ShortcutOverride: accept to prevent Qt from stealing the shortcut
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }

    // KeyPress: perform the action
    if (isCopyShortcut) {
        if (!m_terminal->selectedText().isEmpty())
            m_terminal->copyClipboard();           // Copy selected text
        else
            sendCtrlC();                            // Send SIGINT (no selection)
    } else {
        m_terminal->pasteClipboard();              // Paste from clipboard
    }
    return true;
}
```

**Smart Cmd+C behavior:** When text is selected, Cmd+C copies it to the system clipboard. When nothing is selected, it sends Ctrl+C (SIGINT) to the terminal process — matching iTerm2 and other Mac terminals.

**Key event flow (before fix):**
```
User presses Cmd+C
  → TerminalDisplay::keyPressEvent()  (focus proxy receives it)
  → Vt102Emulation::sendKeyEvent()    (sends garbage bytes to shell)
  → TerminalView::keyPressEvent()     (NEVER CALLED)
```

**Key event flow (after fix):**
```
User presses Cmd+C
  → TerminalView::eventFilter()       (intercepts on focus proxy)
  → copyClipboard() or sendCtrlC()    (correct action)
  → Event consumed (never reaches emulation)
```

**Gotcha:** Always use `widget->focusProxy()` when installing event filters on QTermWidget. The public QTermWidget never has focus — its internal TerminalDisplay does.

### 3.4 Drag-and-Drop: Overriding QTermWidget's Built-in Handling

QTermWidget's `TerminalDisplay` has basic drag-and-drop (file URLs → quoted paths). We override it in `TerminalView` for:
- **Visual feedback** — purple border while dragging over the terminal
- **Clipboard image paste** — Cmd+V saves screenshot to `<project>/screenshots/`, inserts path
- **Consistent control** — all input handling in one place

```cpp
// In TerminalView::setupUi():
m_terminal->setAcceptDrops(false);
QWidget *display = m_terminal->focusProxy();
if (display)
    display->setAcceptDrops(false);
```

Then `TerminalView` handles `dragEnterEvent`, `dragLeaveEvent`, `dropEvent`, and `paintEvent` (for the purple border overlay).

**Shell escaping** for dropped paths uses single-quote wrapping:
```cpp
// '/path/with spaces' → works
// '/path/it'\''s/here' → handles embedded quotes
```

**Image paste flow (Cmd+V with screenshot in clipboard):**
```
Clipboard has image?
  → Yes: save to <project>/screenshots/screenshot-<timestamp>.png
         insert shell-escaped path into terminal
  → No:  fall through to normal text paste
```

### 3.5 Keyboard Translation: Shift+Return Fix

**Problem:** Shift+Return produced "OM" instead of a newline.

**Root cause:** `lib/qtermwidget/lib/kb-layouts/default.keytab` line 33:
```
key Return+Shift : "\EOM"
```

`\EOM` is the VT220 escape sequence `ESC O M` (application keypad Enter). The shell receives this literally — ESC is invisible, "OM" appears as text.

**Fix:** Changed to `"\n"` in `default.keytab` only:
```
key Return+Shift : "\n"
```

This is cross-platform — the keytab maps Qt key codes (same on all OSes) to byte sequences. Claude Code uses Shift+Enter for multiline input, so this sends the expected newline.

**Important:** OS-specific keytabs (e.g. `macbook.keytab`) are separate translators, NOT overlays on `default.keytab`. Only `default.keytab` is loaded by default. See [046_terminal_keymappings.md](046_terminal_keymappings.md) for full details on the keytab architecture.

---

## 4. Multi-Window Architecture

### 4.1 Window Manager Pattern

Use a singleton `WindowManager` to coordinate windows:

```cpp
class WindowManager : public QObject {
    static WindowManager *s_instance;
    MainWindow *m_mainWindow = nullptr;
    QList<QPointer<TerminalWindow>> m_terminalWindows;

public:
    static WindowManager *instance();
    void showMainWindow();
    void openTerminalWindow(const QString &projectPath);
};
```

### 4.2 QPointer for Window Tracking

Use `QPointer` for safe tracking of windows that may be deleted:

```cpp
QList<QPointer<TerminalWindow>> m_terminalWindows;

void WindowManager::onTerminalWindowDestroyed(QObject *obj) {
    for (int i = m_terminalWindows.size() - 1; i >= 0; --i) {
        if (m_terminalWindows[i].isNull() || m_terminalWindows[i] == obj) {
            m_terminalWindows.removeAt(i);
        }
    }
}
```

**Gotcha:** Regular pointers become dangling when windows are closed. `QPointer` auto-nulls.

---

## 5. Prerequisite Detection

### 5.1 Cross-Platform Executable Search

Search PATH manually for executables:

```cpp
QString PrerequisiteInstaller::searchInPath(const QString &program,
                                             const QProcessEnvironment &env) {
#ifdef Q_OS_WIN
    QChar separator = ';';
    QStringList extensions = {".exe", ".cmd", ".bat", ""};
#else
    QChar separator = ':';
    QStringList extensions = {""};
#endif

    QStringList paths = env.value("PATH").split(separator, Qt::SkipEmptyParts);
    for (const QString &path : paths) {
        for (const QString &ext : extensions) {
            QString fullPath = path + "/" + program + ext;
            QFileInfo fileInfo(fullPath);
            if (fileInfo.exists() && fileInfo.isExecutable()) {
                return fullPath;
            }
        }
    }
    return QString();
}
```

### 5.2 Homebrew Detection

Check both PATH and common installation paths:

```cpp
bool PrerequisiteInstaller::isHomebrewInstalled(const QProcessEnvironment &env) {
    // Check PATH first
    if (!searchInPath("brew", env).isEmpty()) {
        return true;
    }

    // Check common paths
    QStringList commonPaths = {
        "/opt/homebrew/bin/brew",      // Apple Silicon
        "/usr/local/bin/brew",         // Intel Mac
        "/home/linuxbrew/.linuxbrew/bin/brew"  // Linux
    };

    for (const QString &path : commonPaths) {
        QFileInfo fileInfo(path);
        if (fileInfo.exists() && fileInfo.isExecutable()) {
            return true;
        }
    }
    return false;
}
```

### 5.3 Linux Package Manager Detection

Detect available package manager in preference order:

```cpp
QString PrerequisiteInstaller::detectLinuxPackageManager(const QProcessEnvironment &env) {
    QStringList packageManagers = {"apt", "dnf", "yum", "pacman", "zypper"};

    for (const QString &pm : packageManagers) {
        if (!searchInPath(pm, env).isEmpty()) {
            return pm;
        }
    }
    return QString();
}
```

---

## 6. Testing Patterns

### 6.1 Headless Testing

All Qt tests run with `QT_QPA_PLATFORM=offscreen`:

```cmake
set_tests_properties(${PROJECT_NAME}_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

**Gotcha:** `ctest` uses this automatically. Running the test binary directly requires manual env setup.

### 6.2 testable Widget Naming

Every testable widget needs `setObjectName()`:

```cpp
m_terminal = new QTermWidget(0, this);
m_terminal->setObjectName("terminalWidget");
```

Tests find widgets by name:

```cpp
auto *terminal = window.findChild<QTermWidget*>("terminalWidget");
QVERIFY(terminal);
```

### 6.3 Source Directory in Tests

Define `SRCDIR` for accessing test fixtures:

```cmake
target_compile_definitions(${PROJECT_NAME}_tests PRIVATE
    SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}"
    ENABLE_TEST_API
)
```

---

## 7. CMake Patterns

### 7.1 Platform Detection

```cmake
if(APPLE)
    set(PLATFORM_MACOS TRUE)
elseif(WIN32)
    set(PLATFORM_WINDOWS TRUE)
elseif(UNIX AND NOT APPLE)
    set(PLATFORM_LINUX TRUE)
endif()
```

### 7.2 QTermWidget Integration

Build patched QTermWidget from source for Windows ConPTY support:

```cmake
option(USE_SYSTEM_QTERMWIDGET "Use system QTermWidget" OFF)

if(USE_SYSTEM_QTERMWIDGET)
    find_package(qtermwidget6 REQUIRED)
else()
    add_subdirectory(lib/qtermwidget)
endif()
```

### 7.3 macOS Bundle Configuration

```cmake
set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "${APP_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_IDENTIFIER}"
    MACOSX_BUNDLE_ICON_FILE "maude.icns"
)
```

### 7.4 Windows Console Visibility

Show console in Debug, hide in Release:

```cmake
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE FALSE)
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
```

---

## 8. Platform-Specific Gotchas

### 8.1 macOS

| Issue | Solution |
|-------|----------|
| Gatekeeper blocks app | Remove quarantine: `xattr -rd com.apple.quarantine App.app` |
| SF Pro font missing | Fall back to `QApplication::font()` |
| Qt::ControlModifier | Maps to Cmd key, not Ctrl |
| Icon not showing | Place in Resources with proper bundle config |

### 8.2 Windows

| Issue | Solution |
|-------|----------|
| DLL not found | Run `windeployqt.exe` after build |
| UTF-8 encoding | Call `SetConsoleOutputCP(CP_UTF8)` at startup |
| Console flash | Set `WIN32_EXECUTABLE TRUE` for Release |
| ConPTY not available | Requires Windows 10 1809+ |

### 8.3 Linux

| Issue | Solution |
|-------|----------|
| xcb plugin not found | `apt install libxcb-cursor0` |
| Fonts look bad | Install `fonts-dejavu` |
| Native dialogs broken | `apt install qt6-gtk-platformtheme` |
| inotify limit | Increase `/proc/sys/fs/inotify/max_user_watches` |

---

## 9. Performance Optimizations

### 9.1 Build Speed

Disable LTO and unity builds for development:

```cmake
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
    set(CMAKE_UNITY_BUILD OFF)
endif()
```

Target: < 3 seconds for small changes.

### 9.2 Folder Cache

Use background thread for scanning with BFS:

```cpp
PathCacheManager::instance()->startScan();  // Runs in QThread
```

Cache stores paths in `QStringList` with `QSet` for O(1) lookup.

---

## 10. SwiftUI-like Styling

### 10.1 Layout Discipline

Always use stack layouts, not grids:

```cpp
auto *layout = new QVBoxLayout;
layout->setSpacing(8);
layout->setContentsMargins(16, 16, 16, 16);
```

Allowed spacing values: `4 / 8 / 16 / 24` only.

### 10.2 Button Styling

```css
QPushButton {
    padding: 6px 14px;
    border-radius: 6px;
    background: transparent;
    border: none;
}

QPushButton:hover {
    background: rgba(0, 0, 0, 0.06);
}
```

### 10.3 Animation Constants

| Property | Value |
|----------|-------|
| Duration | 120–220ms |
| Easing | `QEasingCurve::OutCubic` |
| Trigger | State changes only |

---

## 11. Common Mistakes to Avoid

1. **Setting style after QApplication creation** — Style must be set before
2. **Using `allKeys().isEmpty()` on macOS QSettings** — Includes system settings
3. **Regular pointers for window tracking** — Use `QPointer` instead
4. **Ctrl+C for copy in terminal apps** — Reserved for SIGINT on Linux/Windows
5. **Running tests without `QT_QPA_PLATFORM=offscreen`** — GUI windows will appear
6. **Assuming SF Pro font is available** — Always provide fallback
7. **Hardcoded pixel sizes** — Use `QSizePolicy` instead
8. **Missing `setObjectName()` on widgets** — Breaks testability
9. **Overriding `keyPressEvent()` on QTermWidget wrappers** — Events go to the internal `TerminalDisplay` via focus proxy, not your wrapper. Use `eventFilter()` on `widget->focusProxy()` instead (see §3.3)

---

## 12. File Locations Summary

| Category | Path | Purpose |
|----------|------|---------|
| App settings | `~/.maude/settings.ini` | Window size, font, preferences |
| Claude config | `~/.maude/claude-config/` | Isolated Claude configuration |
| Logs | `~/.maude/logs/` | Debug output |
| Cache | `~/.maude/cache/` | Folder search cache |
| Projects | `~/.maude/projects.json` | Recent projects list |
