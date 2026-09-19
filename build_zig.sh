#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/src"
"${EDGECAT_ZIG:-zig}" c++ -target x86_64-windows-gnu -std=c++17 -Os \
  -Wall -Wextra -municode -Wl,--subsystem,windows -static \
  main.cpp app.rc -lgdiplus -lcomctl32 -lcomdlg32 -lshell32 \
  -lole32 -luser32 -lgdi32 -ladvapi32 -o ../EdgeCatTimer.exe
