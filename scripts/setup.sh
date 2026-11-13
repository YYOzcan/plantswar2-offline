#!/bin/bash
# scripts/setup.sh <Plants War 2 1.0.6 APK (com.ftt.pw2.global.gl)>
# Unpacks your copy of the original game into apk/ (raw) and apk_smali/ (apktool), applies the data patches
# and creates a signing key. Then run scripts/build.sh.
set -e
. "$(dirname "$0")/env.sh"
cd "$PW2"
IN="$(readlink -f "${1:?usage: scripts/setup.sh <original Plants War 2 1.0.6 APK>}")"
EXPECT=df2a4a5db8e235e6039c460cfcfe52c354d8a4a5bb238d96dee6e87257c6edea
SUM=$(sha256sum "$IN" | cut -d' ' -f1)
[ "$SUM" = "$EXPECT" ] || echo "warning: unexpected APK checksum $SUM (tested with 1.0.6 from APKPure)"
rm -rf apk apk_smali
mkdir apk && (cd apk && unzip -q "$IN")
java -jar "$APKTOOL" d -q -f -o apk_smali "$IN"
python3 scripts/patch_apk.py apk_smali
python3 scripts/patch_data.py
[ -f pw2.keystore ] || keytool -genkeypair -keystore pw2.keystore -storepass pw2pass -keypass pw2pass \
    -alias pw2 -keyalg RSA -keysize 2048 -validity 36500 -dname "CN=Plants War 2 Offline" >/dev/null
echo "setup done, now: scripts/build.sh"
