#!/bin/bash
# Plants War 2 - offline launcher: private server + Android emulator + game.
PW2="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
export ANDROID_SDK_ROOT="$PW2/android-sdk" ANDROID_AVD_HOME="$PW2/avd"
ADB="$PW2/android-sdk/platform-tools/adb"
EMU="$PW2/android-sdk/emulator/emulator"
PKG=com.ftt.pw2.global.gl
LOG="$PW2/launcher.log"
GPU="${PW2_GPU:-host}"          # host (RTX), or swiftshader_indirect as a fallback

notify() { command -v notify-send >/dev/null && notify-send -a "Plants War 2" "Plants War 2" "$1"; echo "$1"; }

# 1) private server
if ! { [ -f "$PW2/server/server.pid" ] && kill -0 "$(cat "$PW2/server/server.pid")" 2>/dev/null; }; then
    "$PW2/server/restart.sh"
fi

# 2) emulator (reuse a running one)
if ! "$ADB" get-state >/dev/null 2>&1; then
    notify "Starting..."
    nohup "$EMU" -avd pw2 -gpu "$GPU" -no-boot-anim -no-audio -feature -Vulkan > "$LOG" 2>&1 &
    EMU_PID=$!
    "$ADB" wait-for-device
    until [ "$("$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; do
        kill -0 $EMU_PID 2>/dev/null || { notify "Emulator failed to start, see $LOG"; exit 1; }
        sleep 2
    done
fi

# 3) install the patched game if needed, then launch it
if ! "$ADB" shell pm path "$PKG" >/dev/null 2>&1; then
    "$ADB" install -r "$PW2/build/pw2.apk" >/dev/null
fi
"$ADB" shell settings put system accelerometer_rotation 0 >/dev/null 2>&1
"$ADB" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1

# 4) when the emulator window is closed, stop the server too
if [ -n "$EMU_PID" ]; then
    wait $EMU_PID
    kill "$(cat "$PW2/server/server.pid")" 2>/dev/null
fi
