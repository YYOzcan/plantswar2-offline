#!/bin/bash
# test/drive.sh shot [name]        -> /tmp/.../scratchpad/drive/<name>.jpg (960x540)
# test/drive.sh tap X Y [wait]     -> tap at half-scale coords (X*2, Y*2), then wait (default 2s)
# test/drive.sh swipe X1 Y1 X2 Y2  -> half-scale swipe
# test/drive.sh key N | text STR | log
. "$(dirname "$0")/../scripts/env.sh"
D=/tmp/claude-1000/-home-yigit/7c8f5296-ed69-43d4-94bb-e0628f35ddb5/scratchpad/drive; mkdir -p $D
case "$1" in
  shot) timeout 20 "$ADB" exec-out screencap -p > $D/raw.png && python3 -c "
from PIL import Image; Image.open('$D/raw.png').convert('RGB').resize((960,540)).save('$D/${2:-s}.jpg', quality=70)" && echo $D/${2:-s}.jpg ;;
  tap) timeout 10 "$ADB" shell input tap $(($2*2)) $(($3*2)); sleep ${4:-2} ;;
  swipe) timeout 10 "$ADB" shell input swipe $(($2*2)) $(($3*2)) $(($4*2)) $(($5*2)) ${6:-400}; sleep 2 ;;
  key) timeout 10 "$ADB" shell input keyevent $2; sleep 2 ;;
  text) timeout 10 "$ADB" shell input text "$2"; sleep 1 ;;
  log) timeout 10 "$ADB" logcat -d -s PW2Text:W PW2Server:I AndroidRuntime:E DEBUG:F | tail -${2:-15} ;;
esac
