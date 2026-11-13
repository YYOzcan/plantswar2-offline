#!/bin/bash
# Visit the lobby screens (1920x1080 emulator), screenshot each, then list missing text ids (PW2Text).
cd ~/pw2; A=android-sdk/platform-tools/adb; OUT=${1:-test/tour}; mkdir -p $OUT
tap() { timeout 10 $A shell input tap $1 $2; sleep ${3:-3}; }
shot() { timeout 15 $A exec-out screencap -p > $OUT/$1.png; }
back() { timeout 10 $A shell input keyevent 4; sleep 2; }
declare -A B=( [gear]="122 905" [minions]="262 905" [skills]="400 905" [stats]="540 905" [friends]="680 905"
  [quest]="817 905" [skin]="953 905" [shop]="1093 905" [mail]="1729 180" [guide]="1729 290" [daily]="1729 400"
  [options]="1729 510" [bless]="147 520" [premium]="147 680" [battle]="1697 910" [hero]="124 60" )
for k in gear minions skills stats friends quest skin shop mail guide daily options bless premium hero battle; do
    tap ${B[$k]} 4; shot $k; back
done
timeout 10 $A logcat -d -s PW2Text:W | grep -o "PW2Text *: .*" | sort | uniq -c | sort -rn
