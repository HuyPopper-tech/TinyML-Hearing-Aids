#!/usr/bin/env bash
# =============================================================================
#  build.sh - one-command build for M55M1_GestureAI (Linux / macOS / WSL)
#
#  Usage:
#    ./build.sh           configure + build (Release)
#    ./build.sh clean      remove build/ then configure + build
#    ./build.sh config     configure only
#    ./build.sh only       build only (skip configure)
#
#  Requires Arm Compiler for Embedded 6.24+ on PATH, plus CMake and Ninja.
# =============================================================================
set -e

PROJ_ROOT="$(cd "$(dirname "$0")" && pwd)"
TC="${PROJ_ROOT}/cmake/armclang.cmake"

if [ "$1" = "clean" ]; then
    rm -rf "${PROJ_ROOT}/build"
fi

if [ "$1" = "only" ]; then
    cmake --build build
    exit 0
fi

cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="${TC}" -DCMAKE_BUILD_TYPE=Release -Wno-dev

# Mirror build/compile_commands.json to the project root for clangd / editors
# that only look at the root. (CMake generates it during the configure above.)
if [ -f "${PROJ_ROOT}/build/compile_commands.json" ]; then
    ln -sf build/compile_commands.json "${PROJ_ROOT}/compile_commands.json"
fi

if [ "$1" = "config" ]; then
    exit 0
fi

cmake --build build

echo ""
echo "Build OK. Output in build/:"
ls -1 "${PROJ_ROOT}/build"/M55M1_*
