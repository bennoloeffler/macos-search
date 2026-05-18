# Qt Development Reference

Essential Qt 6 knowledge extracted from maude-cp-v2 development.

---

## 1. Technology Stack

| Component | Choice | Why |
|-----------|--------|-----|
| Language | C++ | Qt native |
| Framework | Qt 6 Widgets | Terminal embedding works |
| Terminal | QTermWidget | Battle-tested, Konsole engine |
| Build | CMake + Ninja | Fast incremental |

### Why NOT These

| Rejected | Reason |
|----------|--------|
| QML | Focus/IME issues with terminal |
| Electron | WebView latency, RAM heavy |
| Flutter | No native terminal |
| xterm.js | IME bugs, not true native |

---

## 2. Terminal Integration

### Minimal Setup

```cpp
auto* terminal = new QTermWidget(0, parent);
terminal->setShellProgram("/usr/local/bin/claude");
terminal->setArgs({"code"});
terminal->setWorkingDirectory(projectPath);
terminal->startShellProgram();
```

### What QTermWidget Handles

- True PTY (forkpty on Unix, ConPTY on Windows)
- VT100/xterm compatibility
- Colors, cursor, scrollback
- Copy/paste, resize

### What NOT to Do

| Don't | Why |
|-------|-----|
| QProcess + QTextEdit | Not a terminal |
| xterm.js | WebView issues |
| Custom VT parser | Massive effort |

---

## 3. Build Commands

```bash
# Configure (macOS)
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt@6)

# Configure (Windows)
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2022_64

# Configure (Linux)
cmake -B build -G Ninja

# Build
cmake --build build

# Run
./build/maude-cp-v3          # macOS/Linux
.\build\maude-cp-v3.exe      # Windows
```

### Fast Iteration Target

**< 3 seconds for small changes**

- No unity builds
- No LTO in dev
- Debug build default

---

## 4. Platform: macOS

### Installation

```bash
brew install qt@6
export CMAKE_PREFIX_PATH=$(brew --prefix qt@6)
```

### App Bundle

```cmake
set_target_properties(app PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.app"
)
```

### Keyboard

- `Qt::ControlModifier` = Cmd (⌘) on macOS
- Use `QKeySequence::StandardKey` for portability

### Deployment

```bash
macdeployqt build/app.app -dmg
codesign --deep --force --sign "Developer ID" app.app
```

### PTY

Uses `forkpty()` from `<util.h>`. QTermWidget handles it.

---

## 5. Platform: Windows

### Installation

Download Qt Online Installer → MSVC 2022 64-bit

### ConPTY

- Requires Windows 10 1809+
- QTermWidget handles it internally
- Link `onecore.lib` or dynamic load

### Console Window

```cmake
# Hide console for GUI app
set_target_properties(app PROPERTIES WIN32_EXECUTABLE TRUE)

# Show console in debug (for qDebug output)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(app PROPERTIES WIN32_EXECUTABLE FALSE)
endif()
```

### Paths

```cpp
// Qt uses forward slashes internally
QString path = "C:/Users/me/projects";

// Convert for display
QString native = QDir::toNativeSeparators(path);  // C:\Users\...
```

### UTF-8

```cpp
#ifdef Q_OS_WIN
SetConsoleOutputCP(CP_UTF8);
SetConsoleCP(CP_UTF8);
#endif
```

### Deployment

```powershell
windeployqt.exe build\app.exe
```

### Common Issues

| Issue | Solution |
|-------|----------|
| DLL not found | Run windeployqt or add Qt bin to PATH |
| Slow startup | Add to antivirus exclusions |
| ConPTY fails | Check Windows version ≥ 1809 |

---

## 6. Platform: Linux

### Installation

```bash
# Ubuntu/Debian
sudo apt install qt6-base-dev qt6-tools-dev cmake ninja-build

# Fedora
sudo dnf install qt6-qtbase-devel cmake ninja-build

# Arch
sudo pacman -S qt6-base cmake ninja
```

### X11 vs Wayland

```bash
QT_QPA_PLATFORM=xcb ./app      # Force X11
QT_QPA_PLATFORM=wayland ./app  # Force Wayland
```

### PTY

Uses `openpty()` from `<pty.h>`. Native and straightforward.

### Fonts

```cpp
// Common monospace fonts
"DejaVu Sans Mono"
"Liberation Mono"
"Noto Sans Mono"
"Ubuntu Mono"
```

### Desktop Integration

```ini
# ~/.local/share/applications/app.desktop
[Desktop Entry]
Type=Application
Name=MyApp
Exec=/path/to/app %U
Icon=myapp
Terminal=false
Categories=Development;
```

### Deployment Options

| Method | Use Case |
|--------|----------|
| AppImage | Universal, single file |
| Flatpak | Sandboxed, auto-updates |
| Snap | Ubuntu ecosystem |
| deb/rpm | Distribution packages |

### Common Issues

| Issue | Solution |
|-------|----------|
| xcb plugin not found | `apt install libxcb-cursor0` |
| Fonts look bad | `apt install fonts-dejavu` |
| Native dialogs wrong | `apt install qt6-gtk-platformtheme` |

---

## 7. Qt Essentials

### Memory Management

```cpp
// QObjects with parent are auto-deleted
auto* widget = new QWidget(parent);  // parent owns it

// No manual delete needed
// When parent dies, widget dies
```

### Signals & Slots

```cpp
// Modern syntax (compile-time checked)
connect(button, &QPushButton::clicked, this, &MyClass::onClicked);

// Lambda
connect(button, &QPushButton::clicked, [this]() {
    doSomething();
});
```

### Layouts

```cpp
auto* layout = new QVBoxLayout(widget);
layout->addWidget(toolbar);
layout->addWidget(content, 1);  // stretch factor
layout->addWidget(statusBar);
```

### Settings

```cpp
QSettings settings("Company", "App");
settings.setValue("fontSize", 14);
int size = settings.value("fontSize", 12).toInt();
```

### JSON

```cpp
QJsonObject obj;
obj["key"] = "value";
QJsonDocument doc(obj);
file.write(doc.toJson());
```

---

## 8. Debugging

### Environment Variables

```bash
QT_DEBUG_PLUGINS=1        # Debug plugin loading
QT_LOGGING_RULES="*=true" # All logging
QT_SCALE_FACTOR=2         # HiDPI testing
QT_QPA_PLATFORM=offscreen # Headless
```

### Debug Output

```cpp
qDebug() << "Value:" << value;
qWarning() << "Warning!";
qCritical() << "Error!";
```

### Platform Tools

| Platform | Tools |
|----------|-------|
| macOS | lldb, Instruments |
| Windows | Visual Studio, WinDbg |
| Linux | gdb, valgrind, strace |

---

## 9. Project Layout

```
/project
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── MainWindow.cpp
│   └── MainWindow.h
├── tests/
└── docs/
```

### CMake Template

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

qt_add_executable(MyApp
    src/main.cpp
    src/MainWindow.cpp
)

target_link_libraries(MyApp PRIVATE Qt6::Core Qt6::Widgets)
```

---

## 10. Quick Reference

### Keyboard Modifiers

| Key | macOS | Windows/Linux |
|-----|-------|---------------|
| Copy | Cmd+C | Ctrl+C |
| Paste | Cmd+V | Ctrl+V |
| Quit | Cmd+Q | Alt+F4 |

### Standard Paths

```cpp
QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
// macOS: ~/Library/Preferences
// Windows: C:/Users/<user>/AppData/Local
// Linux: ~/.config
```

### Find Executable

```cpp
QString path = QStandardPaths::findExecutable("claude");
```
