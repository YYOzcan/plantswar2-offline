#!/bin/bash
# test/jump.sh <area> <stage>: export save, set story progress, import, restart the game
cd "$(dirname "$0")/.."; . scripts/env.sh
F=saves/jump.json
bash scripts/save.sh export $F >/dev/null && python3 test/setprog.py $F $1 $2 && bash scripts/save.sh import $F | head -1
"$ADB" shell am force-stop com.ftt.pw2.global.gl; sleep 1
"$ADB" shell monkey -p com.ftt.pw2.global.gl -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
