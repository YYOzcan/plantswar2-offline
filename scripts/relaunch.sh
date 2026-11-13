#!/bin/bash
# Restart the game on the connected device and save a screenshot to shot.png.
. "$(dirname "$0")/env.sh"
"$ADB" shell am force-stop com.ftt.pw2.global.gl
"$ADB" shell monkey -p com.ftt.pw2.global.gl -c android.intent.category.LAUNCHER 1 >/dev/null
sleep "${1:-20}"
"$ADB" exec-out screencap -p > "$PW2/shot.png"
