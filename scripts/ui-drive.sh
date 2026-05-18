#!/usr/bin/env bash
#
# ui-drive.sh — minimal UI driver for macos-search via AppleScript / System Events.
#
# Subcommands:
#   activate [APP]            Bring APP to front (default: macos-search).
#   type "TEXT" [APP]         Type TEXT into APP's frontmost window.
#   key KEYNAME [APP]         Send a single key (return, escape, tab, down, up).
#   chord MODS+KEY [APP]      Send a key with modifiers, e.g. "cmd+l", "cmd+return".
#                             Modifiers: cmd ctrl alt shift.
#   wait SECONDS              Sleep.
#   running [APP]             Exit 0 if APP is running, else 1.
#   pid [APP]                 Print PID of APP, or fail.
#   quit [APP]                Politely quit APP.
#
# Note: typing/clicking requires the controlling terminal (or its parent IDE)
# to have macOS "Accessibility" permission for "System Events".
# On first run macOS will prompt. Grant via:
#   System Settings → Privacy & Security → Accessibility.

set -euo pipefail

APP_DEFAULT="macos-search"

osa() { /usr/bin/osascript -e "$1"; }

# Refuse to send keystrokes to a different app by accident. Bail out loudly
# if the target process isn't running.
require_running() {
    local app="$1"
    if ! pgrep -x "$app" >/dev/null 2>&1; then
        echo "[ui-drive] refusing: process \"$app\" is not running" >&2
        echo "[ui-drive] (sending keystrokes blindly would hit whatever app is frontmost)" >&2
        exit 3
    fi
}

cmd_activate() {
    local app="${1:-$APP_DEFAULT}"
    require_running "$app"
    osa "tell application \"$app\" to activate" 2>/dev/null || true
    osa "tell application \"System Events\" to set frontmost of process \"$app\" to true" >/dev/null
}

cmd_type() {
    local text="$1"
    local app="${2:-$APP_DEFAULT}"
    require_running "$app"
    cmd_activate "$app"
    # AppleScript keystroke requires escaping double-quotes and backslashes.
    local escaped="${text//\\/\\\\}"
    escaped="${escaped//\"/\\\"}"
    osa "tell application \"System Events\" to keystroke \"$escaped\""
}

cmd_key() {
    local key="$1"
    local app="${2:-$APP_DEFAULT}"
    require_running "$app"
    cmd_activate "$app"
    case "$key" in
        return|enter) osa 'tell application "System Events" to key code 36' ;;
        escape|esc)   osa 'tell application "System Events" to key code 53' ;;
        tab)          osa 'tell application "System Events" to key code 48' ;;
        space)        osa 'tell application "System Events" to key code 49' ;;
        delete|backspace) osa 'tell application "System Events" to key code 51' ;;
        down)         osa 'tell application "System Events" to key code 125' ;;
        up)           osa 'tell application "System Events" to key code 126' ;;
        left)         osa 'tell application "System Events" to key code 123' ;;
        right)        osa 'tell application "System Events" to key code 124' ;;
        *)
            echo "Unknown key: $key" >&2
            exit 2
            ;;
    esac
}

cmd_chord() {
    local chord="$1"
    local app="${2:-$APP_DEFAULT}"
    require_running "$app"
    cmd_activate "$app"

    # Parse "cmd+shift+l" etc.
    IFS='+' read -ra parts <<< "$chord"
    local key=""
    local mods=()
    for p in "${parts[@]}"; do
        case "$(echo "$p" | tr '[:upper:]' '[:lower:]')" in
            cmd|command)  mods+=("command down") ;;
            ctrl|control) mods+=("control down") ;;
            alt|option|opt) mods+=("option down") ;;
            shift)        mods+=("shift down") ;;
            *)            key="$p" ;;
        esac
    done

    if [ -z "$key" ]; then
        echo "chord needs a non-modifier key: $chord" >&2
        exit 2
    fi

    local using=""
    if [ "${#mods[@]}" -gt 0 ]; then
        local joined
        joined="$(IFS=, ; echo "${mods[*]}")"
        using=" using {${joined//,/, }}"
    fi

    case "$key" in
        return|enter)
            osa "tell application \"System Events\" to key code 36$using" ;;
        escape|esc)
            osa "tell application \"System Events\" to key code 53$using" ;;
        tab)
            osa "tell application \"System Events\" to key code 48$using" ;;
        *)
            osa "tell application \"System Events\" to keystroke \"$key\"$using" ;;
    esac
}

cmd_running() {
    local app="${1:-$APP_DEFAULT}"
    pgrep -x "$app" >/dev/null 2>&1
}

cmd_pid() {
    local app="${1:-$APP_DEFAULT}"
    pgrep -x "$app" || { echo "$app not running" >&2; exit 1; }
}

cmd_quit() {
    local app="${1:-$APP_DEFAULT}"

    # Safety: never send a blind Cmd-Q via System Events — if the target has
    # already exited, focus will be on some other app (your editor!) and the
    # keystroke will quit *that*. Always target the named app or its PID.

    # Try by name first (AppleScript routes by bundle/process name).
    if osa "tell application \"$app\" to quit" 2>/dev/null; then
        return 0
    fi

    # Fall back: SIGTERM to the actual PID (gentle, lets Qt clean up).
    local pid
    if pid="$(pgrep -x "$app" 2>/dev/null)" && [ -n "$pid" ]; then
        kill -TERM "$pid" 2>/dev/null || true
        return 0
    fi

    # App not running — nothing to do.
    return 0
}

usage() {
    sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'
}

main() {
    [ $# -lt 1 ] && { usage; exit 2; }
    local sub="$1"; shift
    case "$sub" in
        activate)  cmd_activate "$@" ;;
        type)      cmd_type "$@" ;;
        key)       cmd_key "$@" ;;
        chord)     cmd_chord "$@" ;;
        wait)      sleep "${1:-1}" ;;
        running)   cmd_running "$@" ;;
        pid)       cmd_pid "$@" ;;
        quit)      cmd_quit "$@" ;;
        -h|--help|help) usage; exit 0 ;;
        *)
            echo "Unknown subcommand: $sub" >&2
            usage
            exit 2
            ;;
    esac
}

main "$@"
