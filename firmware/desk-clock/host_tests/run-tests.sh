#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${TMPDIR:-/tmp}/desk-clock-host-tests"
mkdir -p "$BUILD_DIR"

c++ -std=c++17 -Wall -Wextra -Werror \
    -I"$ROOT/components/lunar/include" \
    "$ROOT/components/lunar/lunar.cpp" \
    "$ROOT/host_tests/test_lunar.cpp" \
    -o "$BUILD_DIR/test_lunar"

"$BUILD_DIR/test_lunar"


c++ -std=c++17 -Wall -Wextra -Werror \
    -I"$ROOT/components/board/include" \
    "$ROOT/components/board/orientation_math.cpp" \
    "$ROOT/host_tests/test_orientation.cpp" \
    -o "$BUILD_DIR/test_orientation"

"$BUILD_DIR/test_orientation"


c++ -std=c++17 -Wall -Wextra -Werror \
    -I"$ROOT/components/board/include" \
    "$ROOT/components/board/battery_math.cpp" \
    "$ROOT/host_tests/test_battery.cpp" \
    -o "$BUILD_DIR/test_battery"
"$BUILD_DIR/test_battery"

c++ -std=c++17 -Wall -Wextra -Werror \
    -I"$ROOT/components/voice_service/include" \
    "$ROOT/components/voice_service/voice_text.cpp" \
    "$ROOT/host_tests/test_voice_text.cpp" \
    -o "$BUILD_DIR/test_voice_text"
"$BUILD_DIR/test_voice_text"
