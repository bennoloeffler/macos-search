#!/usr/bin/env bash
#
# mem-compare.sh — memory comparison of two macos-search binaries
#
# Runs both binaries with `--bench --bench-root ROOT` on the SAME root,
# parses each JSON report's memory.bytes_per_entry (mach task_info
# phys_footprint delta over the scan, divided by cached entries), and
# prints a small table plus the reduction factor A/B.
#
# This is the harness for gate G2 in docs/200_pathstore_redesign.md:
# point A at the baseline binary (e.g. built from main) and B at the
# candidate binary; the printed factor is the claimed memory reduction.

set -euo pipefail

print_help() {
    cat << 'EOF'
Usage: mem-compare.sh <binary-A> <binary-B> [OPTIONS]

Run two macos-search binaries in --bench mode against the same root and
compare their memory cost per cached entry (memory.bytes_per_entry from
the bench JSON). Prints entries, B/entry for each binary, and the
reduction factor A/B.

Arguments:
  binary-A          Path to the baseline binary
                    (e.g. build-baseline/macos-search.app/Contents/MacOS/macos-search)
  binary-B          Path to the candidate binary

Options:
  -h, --help        Show this help and exit.
  --root PATH       Scan root passed as --bench-root (default: this repo).
  --queries N       Query count passed as --bench-queries (default: 100;
                    the bench clamps to a minimum of 10).

Exit codes:
  0  both runs succeeded, table printed
  1  a bench run failed or its JSON lacked the expected fields
  2  usage error

Example:
  scripts/mem-compare.sh \
      build-baseline/macos-search.app/Contents/MacOS/macos-search \
      build/macos-search.app/Contents/MacOS/macos-search \
      --root ~/projects --queries 50
EOF
}

# Resolve repo root from the script's actual location (handles symlinks).
SOURCE="${BASH_SOURCE[0]}"
while [ -L "$SOURCE" ]; do
    DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
    SOURCE="$(readlink "$SOURCE")"
    [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
done
SCRIPT_DIR="$(cd -P "$(dirname "$SOURCE")" && pwd)"
REPO_ROOT="$(cd -P "$SCRIPT_DIR/.." && pwd)"

BIN_A=""
BIN_B=""
ROOT="$REPO_ROOT"
QUERIES="100"

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            print_help; exit 0 ;;
        --root)
            [ $# -ge 2 ] || { echo "--root needs a value" >&2; exit 2; }
            ROOT="$2"; shift 2 ;;
        --root=*)
            ROOT="${1#--root=}"; shift ;;
        --queries)
            [ $# -ge 2 ] || { echo "--queries needs a value" >&2; exit 2; }
            QUERIES="$2"; shift 2 ;;
        --queries=*)
            QUERIES="${1#--queries=}"; shift ;;
        -*)
            echo "Unknown option: $1" >&2
            echo "Run 'mem-compare.sh --help' for usage." >&2
            exit 2 ;;
        *)
            if [ -z "$BIN_A" ]; then BIN_A="$1"
            elif [ -z "$BIN_B" ]; then BIN_B="$1"
            else echo "Unexpected argument: $1" >&2; exit 2
            fi
            shift ;;
    esac
done

if [ -z "$BIN_A" ] || [ -z "$BIN_B" ]; then
    echo "Need two binaries. Run 'mem-compare.sh --help' for usage." >&2
    exit 2
fi
for BIN in "$BIN_A" "$BIN_B"; do
    if [ ! -x "$BIN" ]; then
        echo "[mem-compare] Not an executable: $BIN" >&2
        exit 2
    fi
done
if [ ! -d "$ROOT" ]; then
    echo "[mem-compare] Root is not a directory: $ROOT" >&2
    exit 2
fi

# Work dir for the two JSON reports. Kept after the run (never rm'd) so
# the raw numbers can be inspected; path is printed at the end.
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/mem-compare.XXXXXX")"

run_bench() {
    # $1 = binary, $2 = label (A|B). Writes $WORK_DIR/$2.json.
    local bin="$1" label="$2"
    echo "[mem-compare] $label: $bin" >&2
    if ! "$bin" --bench --bench-root "$ROOT" --bench-queries "$QUERIES" \
            > "$WORK_DIR/$label.json" 2> "$WORK_DIR/$label.stderr"; then
        echo "[mem-compare] bench run $label FAILED. stderr tail:" >&2
        tail -5 "$WORK_DIR/$label.stderr" >&2 || true
        exit 1
    fi
}

run_bench "$BIN_A" "A"
run_bench "$BIN_B" "B"

# Parse both JSONs and print the table with python3 (no sed/awk JSON hacks).
python3 - "$WORK_DIR/A.json" "$WORK_DIR/B.json" "$BIN_A" "$BIN_B" << 'PYEOF'
import json, sys

path_a, path_b, name_a, name_b = sys.argv[1:5]
rows = []
for path, name in ((path_a, name_a), (path_b, name_b)):
    try:
        with open(path) as f:
            d = json.load(f)
        entries = d["scan"]["folder_count"] + d["scan"]["file_count"]
        bpe = d["memory"]["bytes_per_entry"]
    except (json.JSONDecodeError, KeyError, OSError) as e:
        print(f"[mem-compare] cannot parse {path}: {e}", file=sys.stderr)
        sys.exit(1)
    rows.append((name, entries, bpe))

print()
print(f"{'':2}{'binary':<50} {'entries':>9} {'B/entry':>9}")
for label, (name, entries, bpe) in zip(("A:", "B:"), rows):
    short = name if len(name) <= 50 else "…" + name[-49:]
    print(f"{label:<2}{short:<50} {entries:>9} {bpe:>9.1f}")
print()
if rows[1][2] > 0:
    print(f"reduction factor (A/B): {rows[0][2] / rows[1][2]:.2f}x")
else:
    print("reduction factor (A/B): n/a (B measured 0 B/entry)")
PYEOF

echo
echo "[mem-compare] raw reports kept in: $WORK_DIR"
