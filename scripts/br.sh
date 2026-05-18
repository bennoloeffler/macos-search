#!/usr/bin/env bash
#
# br.sh — Build & Run for macos-search
#
# Convention (mirrors ../maude-cp-v3):
#   build/        — for Claude / CI builds
#   build-benno/  — for human-driven builds
#
# The build dir is chosen by --who:
#   --who=benno   (default) → build-benno/
#   --who=claude            → build/
#
# Default build type is Debug. --no-debug switches to Release.

set -euo pipefail

print_help() {
    cat << 'EOF'
Usage: br [OPTIONS]

Build the macos-search app, then run it.

Options:
  -h, --help        Show this help and exit.
  --who=NAME        Build dir suffix. "benno" → build-benno/ (default).
                                       "claude" → build/.
  --no-debug        Release build (default is Debug).
  -c, --clean       Wipe the build directory before configuring.
  -j, --jobs N      Number of parallel build jobs (default: nproc / sysctl).
  -v, --verbose     Verbose build output.
  --no-run          Build only, don't launch the app.
  --bundle          Run the .app bundle via macOS `open` instead of the bare binary.
                    (Bare binary path = build*/macos-search.app/Contents/MacOS/macos-search)
                    Default off because the bare binary surfaces stdout/stderr.
  --detach          Launch in background and return immediately (PID printed).
                    Combine with --log=PATH to capture stdout/stderr.
                    Used by the screenshot workflow: br --detach && screenshot.sh
  --log=PATH        Redirect app stdout+stderr to PATH (only with --detach).
                    Default with --detach: build*/macos-search.log
  --test            Build and run the test suite (macos-search_tests) instead
                    of the app. Exit code = test suite exit code.

Examples:
  br                    # Debug build + run, build-benno/, prints logs to terminal
  br -c                 # Clean Debug rebuild + run
  br --no-debug         # Release build + run
  br --who=claude -c    # Clean Debug rebuild in build/ (for Claude/CI)
  br -j 4 -v            # 4 jobs, verbose
  br --no-run           # Just build
  br --bundle           # Launch the .app via macOS LaunchServices

EOF
}

WHO="benno"
BUILD_TYPE="Debug"
CLEAN="0"
JOBS=""
VERBOSE="0"
RUN="1"
USE_BUNDLE="0"
DETACH="0"
LOG_PATH=""
TEST_MODE="0"

for arg in "$@"; do
    case "$arg" in
        -h|--help)
            print_help; exit 0 ;;
        --who=*)
            WHO="${arg#--who=}" ;;
        --no-debug)
            BUILD_TYPE="Release" ;;
        -c|--clean)
            CLEAN="1" ;;
        -j*)
            # -j8 or -j 8 both accepted
            JOBS="${arg#-j}"
            ;;
        --jobs=*)
            JOBS="${arg#--jobs=}" ;;
        -v|--verbose)
            VERBOSE="1" ;;
        --no-run)
            RUN="0" ;;
        --bundle)
            USE_BUNDLE="1" ;;
        --detach)
            DETACH="1" ;;
        --log=*)
            LOG_PATH="${arg#--log=}" ;;
        --test)
            TEST_MODE="1" ;;
        *)
            echo "Unknown option: $arg" >&2
            echo "Run 'br --help' for usage." >&2
            exit 2
            ;;
    esac
done

# Resolve repo root from the script's actual location (handles symlinks).
SOURCE="${BASH_SOURCE[0]}"
while [ -L "$SOURCE" ]; do
    DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
    SOURCE="$(readlink "$SOURCE")"
    [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
done
SCRIPT_DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
REPO_ROOT="$(cd -P "$SCRIPT_DIR/.." && pwd)"

case "$WHO" in
    benno)  BUILD_DIR="$REPO_ROOT/build-benno" ;;
    claude) BUILD_DIR="$REPO_ROOT/build" ;;
    *)
        echo "Unknown --who=$WHO (expected: benno|claude)" >&2
        exit 2 ;;
esac

# Default parallelism.
if [ -z "$JOBS" ]; then
    JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
fi

if [ "$CLEAN" = "1" ] && [ -d "$BUILD_DIR" ]; then
    echo "[br] Cleaning $BUILD_DIR"
    # Move to Trash instead of rm -rf, per global instructions.
    if command -v trash >/dev/null 2>&1; then
        trash "$BUILD_DIR"
    else
        # Trash CLI not installed: fall back to mv-aside backup, never rm.
        BACKUP="${BUILD_DIR}.$(date +%Y-%m-%d--%H.%M.%S).claude-backup"
        echo "[br] 'trash' CLI not found — moving build dir aside to: $BACKUP"
        echo "[br] (install with: brew install trash)"
        mv "$BUILD_DIR" "$BACKUP"
    fi
fi

mkdir -p "$BUILD_DIR"

echo "[br] Configuring ($BUILD_TYPE) in $BUILD_DIR"
CMAKE_VERBOSE_FLAG=""
[ "$VERBOSE" = "1" ] && CMAKE_VERBOSE_FLAG="-DCMAKE_VERBOSE_MAKEFILE=ON"

cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    $CMAKE_VERBOSE_FLAG

# Mirror compile_commands.json into the repo root so clangd in editors finds it.
if [ -f "$BUILD_DIR/compile_commands.json" ]; then
    ln -sf "$BUILD_DIR/compile_commands.json" "$REPO_ROOT/compile_commands.json"
fi

BUILD_VERBOSE_FLAG=""
[ "$VERBOSE" = "1" ] && BUILD_VERBOSE_FLAG="-v"

echo "[br] Building (-j$JOBS)"
cmake --build "$BUILD_DIR" -j "$JOBS" $BUILD_VERBOSE_FLAG

if [ "$RUN" = "0" ]; then
    echo "[br] --no-run set, exiting."
    exit 0
fi

if [ "$TEST_MODE" = "1" ]; then
    TEST_BIN="$BUILD_DIR/macos-search_tests"
    if [ ! -e "$TEST_BIN" ]; then
        echo "[br] Test binary not found at: $TEST_BIN" >&2
        exit 1
    fi
    echo "[br] Running tests"
    exec "$TEST_BIN"
fi

APP_BUNDLE="$BUILD_DIR/macos-search.app"
APP_BIN="$APP_BUNDLE/Contents/MacOS/macos-search"

if [ ! -e "$APP_BIN" ]; then
    echo "[br] Build artifact not found at: $APP_BIN" >&2
    exit 1
fi

echo "[br] Launching"
if [ "$DETACH" = "1" ]; then
    [ -z "$LOG_PATH" ] && LOG_PATH="$BUILD_DIR/macos-search.log"
    # nohup + & detaches from this shell; exec via setsid-style redirection.
    : > "$LOG_PATH"
    nohup "$APP_BIN" >"$LOG_PATH" 2>&1 &
    APP_PID=$!
    disown "$APP_PID" 2>/dev/null || true
    echo "[br] detached pid=$APP_PID log=$LOG_PATH"
    # Wait a moment so the window has a chance to appear.
    sleep 0.5
    if ! kill -0 "$APP_PID" 2>/dev/null; then
        echo "[br] app exited immediately. log tail:" >&2
        tail -20 "$LOG_PATH" >&2 || true
        exit 1
    fi
    echo "$APP_PID"
    exit 0
fi
if [ "$USE_BUNDLE" = "1" ]; then
    # Use macOS open; logs go to Console.app, not this terminal.
    open "$APP_BUNDLE"
else
    # Run the binary directly so qDebug/stderr land in this terminal.
    exec "$APP_BIN"
fi
