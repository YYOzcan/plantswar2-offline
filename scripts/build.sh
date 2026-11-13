#!/bin/bash
# Build the self-contained offline APK: patched native lib + embedded Java server + signed APK.
#   scripts/build.sh            -> build/pw2.apk (server inside the app, talks to 127.0.0.1)
#   PW2_HOST=10.0.2.2 scripts/build.sh -> dev build that uses the Python server on the PC (emulator)
set -e
. "$(dirname "$0")/env.sh"
cd "$PW2"
[ -d apk_smali ] || { echo "run scripts/setup.sh <original APK> first"; exit 1; }
HOST="${PW2_HOST:-127.0.0.1}"
T=build/tmp; rm -rf $T; mkdir -p $T/classes

# 0) Java/manifest edits (XIGNCODE off, embedded server start, targetSdk 24); armeabi-v7a only
python3 scripts/patch_apk.py apk_smali >/dev/null
rm -rf apk_smali/lib/armeabi

# 1) native lib: server URLs -> $HOST, GLES1 -> matrix-tracking shim
for abi in armeabi-v7a; do
    python3 scripts/patch_lib.py apk/lib/$abi/libGame.so apk_smali/lib/$abi/libGame.so "$HOST" >/dev/null
    cp glshim/libpw2gl.so apk_smali/lib/$abi/libpw2gl.so
done

# 2) embedded server (offline/src, Java 8, no Android APIs)
javac --release 8 -Xlint:-options -d $T/classes $(find offline/src -name '*.java')
(cd $T/classes && jar cf ../server.jar .)

# 3) apktool + merge the server into the game's classes.dex
java -jar "$APKTOOL" b -q -o $T/unsigned.apk apk_smali
(cd $T && unzip -q -o unsigned.apk classes.dex && mkdir -p dex && \
 $BT/d8 --release --min-api 14 --output dex classes.dex server.jar)
[ "$(ls $T/dex)" = classes.dex ] || { echo "d8 produced: $(ls $T/dex)"; exit 1; }
python3 scripts/replace_dex.py $T/unsigned.apk $T/dex/classes.dex

# 4) align + sign
$BT/zipalign -f -p 4 $T/unsigned.apk $T/aligned.apk
$BT/apksigner sign --ks pw2.keystore --ks-pass pass:pw2pass --out build/pw2.apk $T/aligned.apk
rm -rf $T build/pw2.apk.idsig
echo "built build/pw2.apk (server host $HOST)"
