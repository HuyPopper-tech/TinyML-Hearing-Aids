# =============================================================================
#  armclang.cmake - CMake toolchain file for Arm Compiler 6 (armclang/AC6)
#  Target: Nuvoton M55M1 (Cortex-M55 + Helium MVE + FPU double-precision)
#  Board:  custom M55M1 PCB (M55M1R2LJAE, LQFP64)
#
#  SPDX-License-Identifier: Apache-2.0
#
#  Based on Arm's verified bare-metal-armclang.cmake (ml-embedded-evaluation-kit)
#  adapted for the Nuvoton M55M1 BSP (microlib + Nuvoton scatter file).
#
#  Requirements (must be on PATH):
#    armclang, armasm, armlink, armar, fromelf
#    (Arm Compiler for Embedded / AC6 >= 6.16, BSP uses ^6.24)
#    Activate the license first:  armlm activate ...
#
#  Usage:
#    cmake -B build -G Ninja \
#          -DCMAKE_TOOLCHAIN_FILE=cmake/armclang.cmake \
#          -DCMAKE_BUILD_TYPE=Release
#    cmake --build build
# =============================================================================

# ---- Locate the compiler binaries (resolve to absolute paths) -------------
# We use find_program so that a missing compiler fails LOUDLY at configure
# time instead of CMake silently falling back to a host linker (e.g. MinGW ld),
# which would break the bare-metal link.
find_program(ARMCLANG_BIN  armclang  REQUIRED)
find_program(ARMASM_BIN    armasm    REQUIRED)
find_program(ARMLINK_BIN   armlink   REQUIRED)
find_program(ARMAR_BIN     armar     REQUIRED)
find_program(FROMELF_BIN   fromelf   REQUIRED)

set(CMAKE_C_COMPILER            ${ARMCLANG_BIN})
set(CMAKE_CXX_COMPILER          ${ARMCLANG_BIN})
set(CMAKE_ASM_COMPILER          ${ARMASM_BIN})
set(CMAKE_AR                    ${ARMAR_BIN})
# IMPORTANT: explicitly set the linker so CMake never picks a host ld.exe.
set(CMAKE_LINKER                ${ARMLINK_BIN})
# The target must be known while CMake identifies armclang. Otherwise CMake
# falls back to an unknown compiler configuration, emits Windows `.obj` files,
# and the Nuvoton scatter file cannot select its required `*.o` sections.
set(CMAKE_C_COMPILER_TARGET     arm-arm-none-eabi)
set(CMAKE_CXX_COMPILER_TARGET   arm-arm-none-eabi)
# An unactivated AC6 installation rejects CMake's compiler-ID source before
# emitting an object. Declare the known vendor so CMake can still load its
# built-in ARMClang rules (armlink, armar --via=, and `.o` object names).
set(CMAKE_C_COMPILER_ID         ARMClang)
set(CMAKE_CXX_COMPILER_ID       ARMClang)
set(CMAKE_C_COMPILER_ID_RUN     TRUE)
set(CMAKE_CXX_COMPILER_ID_RUN   TRUE)
# armasm is not armclang, but its startup object must use the `.o` suffix
# expected by the vendor scatter file.
set(CMAKE_ASM_OUTPUT_EXTENSION  ".o")
set(CMAKE_ASM_OUTPUT_EXTENSION_REPLACE 1)

set(CMAKE_CROSSCOMPILING        true)
set(CMAKE_SYSTEM_NAME           Generic)
set(CMAKE_SYSTEM_PROCESSOR      cortex-m55 CACHE STRING "Cortex-M CPU")

# ---- Skip compiler link-test (bare-metal, no libc test possible) -----------
set(CMAKE_C_COMPILER_WORKS      1)
set(CMAKE_CXX_COMPILER_WORKS    1)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# CMake cannot identify Armclang in this bare-metal setup, so it leaves the
# usual Debug flags empty. Force DWARF generation for both application C and
# C++ sources; otherwise Cortex-Debug can attach but cannot resolve source
# breakpoints or function bounds.
set(CMAKE_C_FLAGS_DEBUG "-g" CACHE STRING "C compiler flags for Debug builds" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-g" CACHE STRING "C++ compiler flags for Debug builds" FORCE)

# CMake's built-in ARMClang support now owns the armar/armlink rules. It uses
# `--create -cr`, `--via=`, and `.o` output names required by armlink/scatter.

# ---- Minimum compiler version check ----------------------------------------
set(MIN_ARM_CLANG_VERSION 6.16)

# ---- CPU / FPU / MVE options for Cortex-M55 --------------------------------
# cortex-m55 enables: Helium MVE (integer + float) + double-precision FPU.
# Set M55_CPU to "cortex-m55+cdecp2" if you need ACI custom instructions.
if(NOT DEFINED M55_CPU)
    set(M55_CPU "cortex-m55" CACHE STRING "Target CPU string for -mcpu=")
endif()

set(CPU_COMPILE_OPTION  "-mcpu=${M55_CPU}")
set(FLOAT_ABI_OPTION    "-mfloat-abi=hard")
set(CPU_LINK_OPT        "--cpu=cortex-m55")

# ---- Target triple + arch flags (applied to C/CXX/ASM) ---------------------
add_compile_options(
    --target=arm-arm-none-eabi
    -mthumb
    ${CPU_COMPILE_OPTION}
    ${FLOAT_ABI_OPTION}
    -mlittle-endian
)

# ---- General-purpose compile options (from Nuvoton BSP AC6 config) ---------
add_compile_options(
    -funsigned-char
    -fshort-enums
    -fshort-wchar
    -ffunction-sections
    -fdata-sections
    -Wall
)

# Use microlib for smallest code size (matches Nuvoton BSP AC6 config).
# Add -D__MICROLIB so the CMSIS/startup code selects microlib-aware paths.
add_compile_definitions(__MICROLIB)
add_compile_options(-D__MICROLIB)

# ---- C / C++ language standards --------------------------------------------
add_compile_options(
    "$<$<COMPILE_LANGUAGE:C>:-std=c11>"
    "$<$<COMPILE_LANGUAGE:CXX>:-xc++;-std=c++11;-fno-rtti;-fno-exceptions>"
)

# ---- Link options ----------------------------------------------------------
# armlink reads the scatter (.scf) file; the first line of the Nuvoton .scf
# ("#! armclang -E ...") tells armlink to preprocess it automatically.
set(ARMCLANG_INFO_STR "sizes,totals,unused,veneers,summarysizes")
if(CMAKE_BUILD_TYPE STREQUAL Debug)
    set(ARMCLANG_INFO_STR "${ARMCLANG_INFO_STR},stack,summarystack")
endif()

add_link_options(
    ${CPU_LINK_OPT}
    --library_type=microlib
    --map
    --symbols
    --load_addr_map_info
    --xref
    --callgraph
    --info ${ARMCLANG_INFO_STR}
    --strict
    --diag_suppress=L6439W,L6314W
)

# =============================================================================
# Helper functions
# =============================================================================

# Add the Nuvoton scatter file (.scf) to a target.
#   add_linker_script(<target> <absolute-scatter-path>)
function(add_linker_script TARGET_NAME SCATTER_PATH)
    if(NOT EXISTS "${SCATTER_PATH}")
        message(FATAL_ERROR "Scatter file not found: ${SCATTER_PATH}")
    endif()
    message(STATUS "Linker scatter file: ${SCATTER_PATH}")
    target_link_options(${TARGET_NAME} PRIVATE "--scatter=${SCATTER_PATH}")
endfunction()

# Make armlink write a map file alongside the ELF.
#   add_target_map_file(<target> <map-file-path>)
function(add_target_map_file TARGET_NAME MAP_FILE_PATH)
    target_link_options(${TARGET_NAME} PRIVATE "--list=${MAP_FILE_PATH}")
endfunction()

# Use fromelf to convert the ELF into a raw binary (.bin).
# Uses the $<TARGET_FILE:> generator expression so the ELF path is always
# correct regardless of the suffix CMake assigns (.elf / .axf / none).
# NOTE: fromelf --bin with a directory (trailing slash) names the output
# after the scatter execution region (e.g. "ER_ROM"), so we pass an explicit
# OUTPUT_BIN file path to get a predictable name.
#   add_bin_generation_command(TARGET <t> OUTPUT_BIN <full-path-to-.bin>)
function(add_bin_generation_command)
    set(multiValueArgs "")
    set(oneValueArgs TARGET OUTPUT_BIN)
    cmake_parse_arguments(PARSED "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    add_custom_command(TARGET ${PARSED_TARGET}
        POST_BUILD
        COMMAND ${FROMELF_BIN} --bin
                --output=${PARSED_OUTPUT_BIN}
                $<TARGET_FILE:${PARSED_TARGET}>
        COMMENT "Generating binary: ${PARSED_OUTPUT_BIN}"
    )
endfunction()

# Optionally assert the compiler version at configure time.
function(enforce_compiler_version)
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS ${MIN_ARM_CLANG_VERSION})
        message(FATAL_ERROR
            "Arm Compiler (armclang) version must be ${MIN_ARM_CLANG_VERSION} or greater. "
            "Found: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
endfunction()
