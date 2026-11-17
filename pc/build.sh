#!/bin/sh
# kept for muscle memory: the build lives in build.py now (Linux/Windows/macOS targets)
exec "$(dirname "$0")/../.venv-pc/bin/python" "$(dirname "$0")/build.py" "$@"
