#!/usr/bin/env bash
# Configure (first run) and build via CMake. Requires SDL3 + pkg-config.
set -e
cd "$(dirname "$0")"

BUILD_DIR=build
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    cmake -S . -B "$BUILD_DIR"
fi
cmake --build "$BUILD_DIR"
echo "Built ./$BUILD_DIR/modplayer — run it with ./$BUILD_DIR/modplayer"
