#!/usr/bin/env bash
# =============================================================================
#  build.sh - profile-aware build for M55M1 GestureAI (Linux / macOS / WSL)
#
#  Usage: ./build.sh [release|debug] [build|config|only|clean]
#    ./build.sh                 Release: configure + build (legacy default)
#    ./build.sh debug           Debug:   configure + build into build-debug/
#    ./build.sh debug config    Configure Debug only
#    ./build.sh release only    Build an existing Release tree only
#    ./build.sh clean           Clean + rebuild Release (legacy command)
#    ./build.sh debug clean     Clean + rebuild Debug
# =============================================================================
set -euo pipefail

usage() {
    echo "Usage: ./build.sh [release|debug] [build|config|only|clean]" >&2
    exit 2
}

PROFILE="release"
ACTION="build"

case "${1:-}" in
    release|debug)
        PROFILE="$1"
        shift
        ;;
    build|config|only|clean)
        ;;
    "")
        ;;
    *)
        usage
        ;;
esac

case "${1:-}" in
    build|config|only|clean)
        ACTION="$1"
        shift
        ;;
    "")
        ;;
    *)
        usage
        ;;
esac

[ "$#" -eq 0 ] || usage

if [ "$PROFILE" = "debug" ]; then
    BUILD_DIR="build-debug"
    BUILD_TYPE="Debug"
else
    BUILD_DIR="build"
    BUILD_TYPE="Release"
fi

PROJ_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_PATH="${PROJ_ROOT}/${BUILD_DIR}"
TC="${PROJ_ROOT}/cmake/armclang.cmake"

if [ "$ACTION" = "clean" ]; then
    rm -rf "${BUILD_PATH}"
fi

if [ "$ACTION" != "only" ]; then
    cmake -S "${PROJ_ROOT}" -B "${BUILD_PATH}" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${TC}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DM55M1_DEBUG_UART_IDX=2 \
        -DM55M1_ENABLE_DSP=ON -DM55M1_ENABLE_NN=ON -DM55M1_MODEL_IN_SRAM=ON -DM55M1_ENABLE_ETHOSU_RUNTIME=ON \
        -DM55M1_ENABLE_TFLM=ON \
        -DM55M1_ENABLE_DCACHE=ON \
        -DM55M1_ENABLE_DAC_I2S_OUTPUT=ON \
        -DM55M1_DEEPFIR_MODEL_IN_SRAM=ON \
        -DM55M1_DEEPFIR_MVE=ON \
        -DM55M1_AUDIO_UART_PCM_CAPTURE=OFF -DM55M1_AUDIO_RELEASE_PROBE=OFF -DM55M1_DEEPFIR_COMPILED_REPLAY=OFF \
        -Wno-dev

    # Point clangd at the most recently configured profile.
    if [ -f "${BUILD_PATH}/compile_commands.json" ]; then
        ln -sfn "${BUILD_DIR}/compile_commands.json" "${PROJ_ROOT}/compile_commands.json"
    fi
fi

if [ "$ACTION" = "config" ]; then
    exit 0
fi

cmake --build "${BUILD_PATH}"

echo ""
echo "Build OK. Output in ${BUILD_DIR}/:"
ls -1 "${BUILD_PATH}"/M55M1_*
