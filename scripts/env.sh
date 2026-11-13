# Shared tool locations (source this). Override with environment variables if your tools live elsewhere.
PW2="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ANDROID_SDK="${ANDROID_SDK:-$PW2/android-sdk}"
APKTOOL="${APKTOOL:-$PW2/tools/apktool.jar}"
JDK="${JDK:-$(ls -d "$PW2"/tools/jdk-21* 2>/dev/null | head -1)}"
[ -n "$JDK" ] && export PATH="$JDK/bin:$PATH"
BT="${BT:-$(ls -d "$ANDROID_SDK"/build-tools/* 2>/dev/null | sort -V | tail -1)}"
ADB="${ADB:-$ANDROID_SDK/platform-tools/adb}"
command -v "$ADB" >/dev/null 2>&1 || ADB=adb
