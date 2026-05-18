#!/usr/bin/env bash
#
# screenshot.sh — capture the macos-search window (or any named app's frontmost window).
#
# Output:   screenshots/<YYYY-MM-DD--HH.MM.SS>_<label>.png  (repo-relative)
# Returns:  absolute path of the file on stdout (and exit 0 on success).
#
# Strategy:
#   1. Activate the target app (so its main window is frontmost).
#   2. Ask System Events for window 1's position + size.
#   3. screencapture -R x,y,w,h.
#
# Falls back to a full-screen capture if bounds can't be resolved.
#
# Accessibility permission:
#   Reading window bounds via System Events requires the calling terminal /
#   IDE to have "Accessibility" permission. macOS prompts on first use.
#   Grant in: System Settings → Privacy & Security → Accessibility.

set -euo pipefail

print_help() {
    cat << 'EOF'
Usage: screenshot.sh [OPTIONS] [LABEL]

Capture the frontmost window of a running app.

Options:
  -h, --help           Show this help.
  --app=NAME           Process name (default: macos-search).
  --out=PATH           Explicit output path.
  --fullscreen         Capture entire display instead of a window.
  --no-activate        Don't bring the app forward before capturing.
  --delay=SECONDS      Wait N seconds after activating (default: 0.3).
  --pad=N              Extra pixels around the window (default: 0).

Positional:
  LABEL                Filename slug (default: "shot").

Default output: <repo-root>/screenshots/<timestamp>_<label>.png
EOF
}

APP="macos-search"
LABEL=""
OUT_PATH=""
FULLSCREEN="0"
ACTIVATE="1"
DELAY="0.3"
PAD="0"

for arg in "$@"; do
    case "$arg" in
        -h|--help)     print_help; exit 0 ;;
        --app=*)       APP="${arg#--app=}" ;;
        --out=*)       OUT_PATH="${arg#--out=}" ;;
        --fullscreen)  FULLSCREEN="1" ;;
        --no-activate) ACTIVATE="0" ;;
        --delay=*)     DELAY="${arg#--delay=}" ;;
        --pad=*)       PAD="${arg#--pad=}" ;;
        -*)
            echo "Unknown option: $arg" >&2
            echo "Run 'screenshot.sh --help' for usage." >&2
            exit 2
            ;;
        *)             LABEL="$arg" ;;
    esac
done

LABEL="${LABEL:-shot}"
LABEL="$(printf '%s' "$LABEL" | tr -c '[:alnum:]_-' '-' | sed 's/-\{2,\}/-/g; s/^-//; s/-$//')"
LABEL="${LABEL:-shot}"

SOURCE="${BASH_SOURCE[0]}"
while [ -L "$SOURCE" ]; do
    DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
    SOURCE="$(readlink "$SOURCE")"
    [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
done
SCRIPT_DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
REPO_ROOT="$(cd -P "$SCRIPT_DIR/.." && pwd)"

if [ -z "$OUT_PATH" ]; then
    TS="$(date +%Y-%m-%d--%H.%M.%S)"
    mkdir -p "$REPO_ROOT/screenshots"
    OUT_PATH="$REPO_ROOT/screenshots/${TS}_${LABEL}.png"
fi

if [ "$ACTIVATE" = "1" ]; then
    /usr/bin/osascript <<EOF >/dev/null 2>&1 || true
tell application "System Events"
    if exists process "$APP" then
        set frontmost of process "$APP" to true
    end if
end tell
EOF
    sleep "$DELAY"
fi

# Ask System Events for the bounds of window 1 of the target process.
BOUNDS="$(/usr/bin/osascript <<EOF 2>/dev/null || true
tell application "System Events"
    if exists process "$APP" then
        tell process "$APP"
            if (count of windows) > 0 then
                set p to position of window 1
                set s to size of window 1
                return ((item 1 of p) as text) & "|" & ((item 2 of p) as text) & "|" & ((item 1 of s) as text) & "|" & ((item 2 of s) as text)
            end if
        end tell
    end if
end tell
EOF
)"
BOUNDS="$(printf '%s' "$BOUNDS" | tr -d ' ')"

MODE=""
if [ "$FULLSCREEN" = "1" ] || [ -z "$BOUNDS" ]; then
    /usr/sbin/screencapture -x "$OUT_PATH"
    MODE="fullscreen"
else
    IFS='|' read -r X Y W H <<< "$BOUNDS"
    # Apply padding.
    X=$(( X - PAD ))
    Y=$(( Y - PAD ))
    W=$(( W + 2 * PAD ))
    H=$(( H + 2 * PAD ))
    # Clamp negative offsets.
    [ "$X" -lt 0 ] && X=0
    [ "$Y" -lt 0 ] && Y=0
    /usr/sbin/screencapture -x -R "${X},${Y},${W},${H}" "$OUT_PATH"
    MODE="rect=${X},${Y},${W},${H}"
fi

if [ ! -s "$OUT_PATH" ]; then
    echo "[screenshot] capture failed (file empty or missing): $OUT_PATH" >&2
    exit 1
fi

echo "[screenshot] mode=$MODE app=\"$APP\" → $OUT_PATH" >&2
echo "$OUT_PATH"
