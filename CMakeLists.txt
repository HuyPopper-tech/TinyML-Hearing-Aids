# NuMaker-GestureAI-M55M1 (M55M1R2LJAE) / Arm Compiler 6
cmake_minimum_required(VERSION 3.20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(POLICY CMP0123)
    cmake_policy(SET CMP0123 NEW)
endif()

set(PROJ_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
project(M55M1_GestureAI VERSION 1.0.0 LANGUAGES C CXX ASM)

# Build options (override with -D<name>=<value>).
set(M55M1_DEBUG_UART_IDX 5 CACHE STRING "Debug UART index (0 or 5)")
set(M55M1_FIRMWARE_MODE "dmic" CACHE STRING "Application firmware: dmic or pf01_uart")
set_property(CACHE M55M1_FIRMWARE_MODE PROPERTY STRINGS dmic pf01_uart)
set(M55_CPU "cortex-m55" CACHE STRING "-mcpu value")
set(M55M1_STACK_SIZE "" CACHE STRING "Stack size override, e.g. 0x1000")
set(M55M1_HEAP_SIZE "" CACHE STRING "Heap size override, e.g. 0x1000")
option(M55M1_ENABLE_DSP   "Build & link CMSIS-DSP" ON)
option(M55M1_ENABLE_NN    "Build & link CMSIS-NN (requires DSP)" OFF)
option(M55M1_ENABLE_RTOS2 "Build & link CMSIS-RTOS2/RTX" OFF)
option(M55M1_ENABLE_NPU   "Build the Arm Ethos-U55 NPU self-test" ON)

# The toolchain file consumes this cache variable.
set(M55_CPU "${M55_CPU}" CACHE STRING "-mcpu value" FORCE)

# Derive every StdDriver include directory from its headers.  This covers
# drivers with private subdirectories (emac/, gdma/, scu/, npu/) now and later.
file(GLOB_RECURSE M55M1_STDDRIVER_HEADERS CONFIGURE_DEPENDS
    "${PROJ_ROOT}/libs/StdDriver/inc/*.h")
set(M55M1_STDDRIVER_INC_DIRS)
foreach(M55M1_HEADER IN LISTS M55M1_STDDRIVER_HEADERS)
    get_filename_component(M55M1_HEADER_DIR "${M55M1_HEADER}" DIRECTORY)
    list(APPEND M55M1_STDDRIVER_INC_DIRS "${M55M1_HEADER_DIR}")
endforeach()
list(REMOVE_DUPLICATES M55M1_STDDRIVER_INC_DIRS)

set(M55M1_INC_DIRS
    ${PROJ_ROOT}/libs/CMSIS/Core/Include
    ${PROJ_ROOT}/libs/Device/Nuvoton/M55M1/Include
    ${M55M1_STDDRIVER_INC_DIRS})

set(M55M1_DEFS
    NVT_DCACHE_ON=1
    DEBUG_PORT_UART_IDX=${M55M1_DEBUG_UART_IDX}
    ETHOSU_LOG_SEVERITY=-1)

set(M55M1_DEVICE_SOURCES
    ${PROJ_ROOT}/libs/Device/Nuvoton/M55M1/Source/startup_M55M1.c
    ${PROJ_ROOT}/libs/Device/Nuvoton/M55M1/Source/system_M55M1.c)

# Always compile the complete Nuvoton Standard Driver tree.  When a driver is
# added under src/, CMake regenerates the build and includes it automatically.
file(GLOB_RECURSE M55M1_STDDRIVER_SOURCES CONFIGURE_DEPENDS
    "${PROJ_ROOT}/libs/StdDriver/src/*.c")

# The supplied BSP omits the separate UTCPD support library (utcpdlib.h).
# Keep this automatic: adding that header later re-enables utcpd.c on the next
# CMake regeneration; meanwhile every self-contained StdDriver source builds.
set(M55M1_UTCPD_SOURCE "${PROJ_ROOT}/libs/StdDriver/src/utcpd.c")
if(NOT EXISTS "${PROJ_ROOT}/libs/StdDriver/inc/utcpdlib.h")
    list(REMOVE_ITEM M55M1_STDDRIVER_SOURCES ${M55M1_UTCPD_SOURCE})
    message(STATUS "StdDriver: skipping utcpd.c (utcpdlib.h is not supplied)")
endif()

if(M55M1_ENABLE_DSP)
    set(DSP_ROOT ${PROJ_ROOT}/libs/CMSIS/DSP)
    file(GLOB M55M1_DSP_SOURCES "${DSP_ROOT}/Source/*/arm_*.c")

    add_library(CMSISDSP STATIC ${M55M1_DSP_SOURCES})
    target_include_directories(CMSISDSP PUBLIC
        ${DSP_ROOT}/Include
        ${PROJ_ROOT}/libs/CMSIS/Core/Include
        ${PROJ_ROOT}/libs/Device/Nuvoton/M55M1/Include)
    target_include_directories(CMSISDSP PRIVATE ${DSP_ROOT}/PrivateInclude)
    target_compile_definitions(CMSISDSP PUBLIC
        ARM_MATH_MVEI ARM_MATH_DSP ARM_MATH_MVEF ARM_MATH_MVE_FLOAT16)
    target_compile_options(CMSISDSP PRIVATE
        -flax-vector-conversions=integer
        -Wno-nan-infinity-disabled)
endif()

if(M55M1_ENABLE_NN)
    if(NOT M55M1_ENABLE_DSP)
        message(FATAL_ERROR "M55M1_ENABLE_NN=ON requires M55M1_ENABLE_DSP=ON")
    endif()

    set(NN_ROOT ${PROJ_ROOT}/libs/NN)
    file(GLOB M55M1_NN_SOURCES "${NN_ROOT}/Source/*/arm_*.c")
    add_library(CMSISNN STATIC ${M55M1_NN_SOURCES})
    target_include_directories(CMSISNN PUBLIC ${NN_ROOT}/Include ${NN_ROOT}/Include/Internal)
    target_link_libraries(CMSISNN PUBLIC CMSISDSP)
endif()

if(M55M1_ENABLE_RTOS2)
    set(RTOS2_ROOT ${PROJ_ROOT}/libs/RTOS2)
    set(RTX_ROOT ${PROJ_ROOT}/libs/RTX)
    file(GLOB M55M1_RTX_C_SOURCES
        ${RTX_ROOT}/Source/rtx_*.c
        ${RTX_ROOT}/Config/RTX_Config.c
        ${RTOS2_ROOT}/Source/os_systick.c)

    set(M55M1_RTX_ASM ${RTX_ROOT}/Source/GCC/irq_armv8mml.S)
    set_source_files_properties(${M55M1_RTX_ASM} PROPERTIES LANGUAGE C)
    set_source_files_properties(${M55M1_RTX_ASM} PROPERTIES
        COMPILE_OPTIONS "-x;assembler-with-cpp;-Wno-unused-command-line-argument")

    add_library(CMSISRTOS2 OBJECT ${M55M1_RTX_C_SOURCES} ${M55M1_RTX_ASM})
    target_include_directories(CMSISRTOS2 PUBLIC
        ${RTOS2_ROOT}/Include ${RTX_ROOT}/Include ${RTX_ROOT}/Config ${RTX_ROOT}/Source
        ${M55M1_INC_DIRS})
endif()

if(M55M1_FIRMWARE_MODE STREQUAL "dmic")
    set(M55M1_APP_SOURCES ${PROJ_ROOT}/app/main.c)
elseif(M55M1_FIRMWARE_MODE STREQUAL "pf01_uart")
    set(M55M1_APP_SOURCES ${PROJ_ROOT}/app/pf01_uart_test.c)
else()
    message(FATAL_ERROR "Unsupported M55M1_FIRMWARE_MODE: ${M55M1_FIRMWARE_MODE}")
endif()

if(M55M1_ENABLE_NPU)
    list(APPEND M55M1_APP_SOURCES
        ${PROJ_ROOT}/app/npu/command_stream.cpp
        ${PROJ_ROOT}/app/npu/npu_conv_test.cpp)
endif()

set(ELF_NAME M55M1_GestureAI.axf)
add_executable(${ELF_NAME}
    ${M55M1_DEVICE_SOURCES}
    ${M55M1_STDDRIVER_SOURCES}
    ${M55M1_APP_SOURCES})
set_target_properties(${ELF_NAME} PROPERTIES SUFFIX "")
target_include_directories(${ELF_NAME} PRIVATE ${M55M1_INC_DIRS})
target_compile_definitions(${ELF_NAME} PRIVATE ${M55M1_DEFS})
if(M55M1_ENABLE_NPU)
    target_include_directories(${ELF_NAME} PRIVATE ${PROJ_ROOT}/app/npu)
endif()

if(M55M1_ENABLE_NN)
    target_link_libraries(${ELF_NAME} PRIVATE CMSISNN)
elseif(M55M1_ENABLE_DSP)
    target_link_libraries(${ELF_NAME} PRIVATE CMSISDSP)
endif()
if(M55M1_ENABLE_RTOS2)
    target_link_libraries(${ELF_NAME} PRIVATE CMSISRTOS2)
endif()

if(M55M1_STACK_SIZE)
    target_link_options(${ELF_NAME} PRIVATE "--predefine=\"-DSTACK_SIZE=${M55M1_STACK_SIZE}\"")
endif()
if(M55M1_HEAP_SIZE)
    target_link_options(${ELF_NAME} PRIVATE "--predefine=\"-DHEAP_SIZE=${M55M1_HEAP_SIZE}\"")
endif()

add_linker_script(${ELF_NAME}
    "${PROJ_ROOT}/libs/Device/Nuvoton/M55M1/Source/ARM/M55M1.scf")
add_target_map_file(${ELF_NAME} "${CMAKE_BINARY_DIR}/${ELF_NAME}.map")
add_bin_generation_command(
    TARGET ${ELF_NAME}
    OUTPUT_BIN "${CMAKE_BINARY_DIR}/${ELF_NAME}.bin")

message(STATUS "M55M1_GestureAI: UART${M55M1_DEBUG_UART_IDX}, all StdDriver sources, DSP=${M55M1_ENABLE_DSP}, NN=${M55M1_ENABLE_NN}, RTOS2=${M55M1_ENABLE_RTOS2}, NPU self-test=${M55M1_ENABLE_NPU}")
