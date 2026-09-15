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
