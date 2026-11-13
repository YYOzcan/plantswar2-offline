#!/bin/bash
A=~/pw2/android-sdk/platform-tools/adb
$A shell am force-stop com.ftt.pw2.global.gl
$A logcat -c
$A shell monkey -p com.ftt.pw2.global.gl -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep ${1:-12}
$A exec-out screencap -p > ~/pw2/shot.png
