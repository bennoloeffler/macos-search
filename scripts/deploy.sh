#!/usr/bin/env bash
# Build a Release bundle, embed Qt, sign with the STABLE self-signed identity,
# install to /Applications, and (optionally) launch.
#
# Why the stable identity: ad-hoc signing (`codesign -s -`) produces a new
# cdhash every build, so macOS TCC treats each rebuild as a different app and
# re-prompts for Desktop/Documents/Downloads access every time. Signing with a
# persistent self-signed certificate ("macos-search Benno Loeffler") gives a STABLE
# designated requirement, so a folder grant made once survives all rebuilds.
#
# One-time cert setup (already done on belmac; recreate on a new machine):
#   openssl req -x509 -newkey rsa:2048 -days 3650 -keyout ms.key -out ms.crt \
#     -nodes -subj "/CN=macos-search Benno Loeffler" \
#     -addext "keyUsage=critical,digitalSignature" \
#     -addext "extendedKeyUsage=codeSigning"
#   openssl pkcs12 -export -legacy -in ms.crt -inkey ms.key -out ms.p12 -password pass:mssearch
#   security import ms.p12 -k ~/Library/Keychains/login.keychain-db -P mssearch -T /usr/bin/codesign
#   security find-certificate -c "macos-search Benno Loeffler" -p ~/Library/Keychains/login.keychain-db > ms-pub.crt
#   security add-trusted-cert -r trustRoot -p codeSign -k ~/Library/Keychains/login.keychain-db ms-pub.crt
#
# Usage: scripts/deploy.sh [--no-launch] [--reset-tcc]
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
IDENTITY="macos-search Benno Loeffler"
BUNDLE_ID="de.v-und-s.macos-search"
APP=/Applications/macos-search.app
STAGE="$(mktemp -d)/macos-search.app"
LAUNCH=1
RESET_TCC=0
for a in "$@"; do
  case "$a" in
    --no-launch) LAUNCH=0 ;;
    --reset-tcc) RESET_TCC=1 ;;
    -h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  esac
done

if ! security find-identity -v -p codesigning | grep -q "$IDENTITY"; then
  echo "error: code-signing identity '$IDENTITY' not found/trusted." >&2
  echo "       run the one-time cert setup at the top of this script." >&2
  exit 1
fi

echo "[deploy] building Release…"
cmake -B "$REPO/build-rel" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" >/dev/null
cmake --build "$REPO/build-rel" --target macos-search -j8 >/dev/null

echo "[deploy] staging + embedding Qt frameworks…"
ditto "$REPO/build-rel/macos-search.app" "$STAGE"
"$(brew --prefix qt)/bin/macdeployqt" "$STAGE" >/dev/null 2>&1 || true

echo "[deploy] signing with stable identity '$IDENTITY'…"
codesign --force --deep --sign "$IDENTITY" "$STAGE"
codesign --verify --deep --strict "$STAGE"
echo "[deploy] designated requirement:"
codesign -d -r- "$STAGE" 2>&1 | grep "designated =>" | sed 's/^/  /'

# Quit a running instance so the bundle can be replaced.
osascript -e "tell application \"System Events\"
  if exists (processes where name is \"macos-search\") then
    tell application \"macos-search\" to quit
  end if
end tell" >/dev/null 2>&1 || true
pkill -x macos-search 2>/dev/null || true
sleep 1

[ -e "$APP" ] && { command -v trash >/dev/null && trash "$APP" || mv "$APP" "$APP.$(date +%s).bak"; }
ditto "$STAGE" "$APP"

if [ "$RESET_TCC" = 1 ]; then
  echo "[deploy] resetting TCC for $BUNDLE_ID (one clean prompt next launch)…"
  tccutil reset All "$BUNDLE_ID" >/dev/null 2>&1 || true
fi

echo "[deploy] installed → $APP"
[ "$LAUNCH" = 1 ] && { open "$APP"; echo "[deploy] launched."; }
