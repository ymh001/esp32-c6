#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${TMPDIR:-/tmp}/desk-clock-energy-view-build"
cmake -S "$ROOT/host_tests/energy_view" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BUILD_DIR" -j 8
"$BUILD_DIR/energy_view_test" "$BUILD_DIR/energy-view.ppm"
"$BUILD_DIR/devices_view_test" "$BUILD_DIR/devices-view.ppm"
