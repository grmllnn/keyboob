#!/usr/bin/env bash
# ponytail: thin wrapper — no logic beyond cmake invocations
set -euo pipefail
cd "$(dirname "$0")"
BUILD="${BUILD:-build}"
FCITX="${KEYBOOP_BUILD_FCITX:-ON}"
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DKEYBOOP_BUILD_FCITX="$FCITX"
cmake --build "$BUILD"
ctest --test-dir "$BUILD" --output-on-failure
