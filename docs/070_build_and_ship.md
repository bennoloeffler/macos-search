# Build & Ship

## Build (as built)

- **Toolchain**: CMake ≥ 3.21, Qt 6 from Homebrew (`brew install qt`),
  Apple Clang (`/usr/bin/c++`), C++17.
- **CMakeLists** at repo root:
  - `qt_add_executable(macos-search MACOSX_BUNDLE ...)` — proper `.app` bundle.
  - `qt_add_executable(macos-search_tests ...)` — bare CLI binary, no bundle.
  - `find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Test)`.
  - Auto-detects Homebrew Qt at `/opt/homebrew/opt/qt` (Apple Silicon) or
    `/usr/local/opt/qt` (Intel).
  - `CMAKE_EXPORT_COMPILE_COMMANDS=ON` — clangd / IDEs work without extra config.
- **Two build dirs by convention**:
  - `build/` — Claude / CI builds (`./br --who=claude`).
  - `build-benno/` — Benno's builds (`./br` default).
- **Entry script**: `./br` → `scripts/br.sh`.
  - Flags: `-c` clean, `--no-debug` Release, `-j N`, `-v`, `--no-run`,
    `--bundle`, `--detach`, `--log=PATH`, `--test`.
  - `--clean` uses `trash` (or moves aside as `.claude-backup`); never `rm -rf`.

## Bundle metadata (today)

In `CMakeLists.txt`:

```cmake
MACOSX_BUNDLE_BUNDLE_NAME       = "macos-search"
MACOSX_BUNDLE_GUI_IDENTIFIER    = "de.v-und-s.macos-search"
MACOSX_BUNDLE_BUNDLE_VERSION    = "0.1.0"
MACOSX_BUNDLE_SHORT_VERSION     = "0.1.0"
```

No icon (`.icns`) yet. No `Info.plist` template — Qt's default is used.

## Not yet built

- **Code signing / notarization**. The bundle is currently unsigned;
  macOS will Gatekeeper-block it on first launch on machines other than
  Benno's. Tracked in `090_open_questions.md` — needs Developer ID and a
  policy decision (sign+notarize vs. internal-only).
- **DMG packaging**.
- **Sparkle / SMAppService updates**.
- **Universal binary** (Intel slice). Today's build is arm64 only — see
  `file build/macos-search.app/Contents/MacOS/macos-search`.

## Distribute (today)

Drag-copy the `.app` bundle out of `build/macos-search.app` or
`build-benno/macos-search.app`. That's it.

## Minimum macOS

Whatever Qt 6.9 supports — currently macOS 12 Monterey. Not pinned in
`CMakeLists.txt` via `CMAKE_OSX_DEPLOYMENT_TARGET`; would be set there
when first distributing externally.
