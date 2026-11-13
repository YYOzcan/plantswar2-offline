#!/bin/bash
set -e
cd ~/pw2
export PATH=~/pw2/tools/jdk-21.0.13+9/bin:$PATH BT=~/pw2/android-sdk/build-tools/34.0.0
java -jar tools/apktool.jar b -q -o build/pw2_unsigned.apk apk_smali
$BT/zipalign -f -p 4 build/pw2_unsigned.apk build/pw2_aligned.apk
$BT/apksigner sign --ks pw2.keystore --ks-pass pass:pw2pass --out build/pw2.apk build/pw2_aligned.apk
rm -f build/pw2_unsigned.apk build/pw2_aligned.apk build/pw2.apk.idsig
echo built build/pw2.apk
