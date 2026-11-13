#!/bin/bash
cd "$(dirname "$0")"
[ -f server.pid ] && kill "$(cat server.pid)" 2>/dev/null
sleep 0.5
setsid nohup python3 -I pw2server.py > server.out 2>&1 &
echo $! > server.pid
