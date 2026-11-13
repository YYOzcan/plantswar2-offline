#!/bin/bash
# Plants War 2 - offline launcher for PC: Android emulator + the self-contained game APK.
# The game server is built into the APK (it listens on the device's own 127.0.0.1), so nothing
# else has to run on the PC. Save backup/restore: scripts/save.sh export|import.
PW2="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
export ANDROID_SDK_ROOT="$PW2/android-sdk" ANDROID_AVD_HOME="$PW2/avd"
ADB="$PW2/android-sdk/platform-tools/adb"
EMU="$PW2/android-sdk/emulator/emulator"
PKG=com.ftt.pw2.global.gl
APK="$PW2/build/pw2.apk"
LOG="$PW2/launcher.log"
GPU="${PW2_GPU:-host}"          # host (RTX), or swiftshader_indirect as a fallback

notify() { command -v notify-send >/dev/null && notify-send -a "Plants War 2" "Plants War 2" "$1"; echo "$1"; }

# 1) emulator (reuse a running one)
if ! "$ADB" get-state >/dev/null 2>&1; then
    notify "Starting..."
    nohup "$EMU" -avd pw2 -gpu "$GPU" -no-boot-anim -feature -Vulkan > "$LOG" 2>&1 &
    EMU_PID=$!
    "$ADB" wait-for-device
    until [ "$("$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; do
        kill -0 $EMU_PID 2>/dev/null || { notify "Emulator failed to start, see $LOG"; exit 1; }
        sleep 2
    done
fi

# 2) install / update the game when build/pw2.apk changed (app data and the save are kept)
SUM="$(sha1sum "$APK" | cut -d' ' -f1)"
if ! "$ADB" shell pm path "$PKG" >/dev/null 2>&1 || [ "$(cat "$PW2/build/.installed" 2>/dev/null)" != "$SUM" ]; then
    "$ADB" install -r "$APK" >/dev/null && echo "$SUM" > "$PW2/build/.installed"
fi

# 3) launch
"$ADB" shell settings put system accelerometer_rotation 0 >/dev/null 2>&1
"$ADB" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
