#!/bin/bash
# Back up / restore the game save of the device connected over adb (phone or emulator).
#   scripts/save.sh export [file]   (default: saves/pw2-save-<date>.json)
#   scripts/save.sh import <file>
# The game must be running (its built-in server answers on the device's 127.0.0.1:12380).
set -e
PW2="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
ADB="${ADB:-$PW2/android-sdk/platform-tools/adb}"
PORT=12381
"$ADB" forward tcp:$PORT tcp:12380 >/dev/null
trap '"$ADB" forward --remove tcp:$PORT >/dev/null 2>&1' EXIT
URL=http://127.0.0.1:$PORT/pw2/admin/save
case "$1" in
  export)
    out="${2:-$PW2/saves/pw2-save-$(date +%Y%m%d-%H%M%S).json}"; mkdir -p "$(dirname "$out")"
    curl -sf "$URL" -o "$out" && echo "saved $out" ;;
  import)
    [ -f "$2" ] || { echo "usage: $0 import <save.json>"; exit 1; }
    curl -sf -X POST --data-binary @"$2" -H "Content-Type: application/json" "$URL"; echo
    echo "restart the game to load it" ;;
  *) echo "usage: $0 export [file] | import <file>"; exit 1 ;;
esac
