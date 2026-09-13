# NuMaker-GestureAI-M55M1 (M55M1R2LJAE) / Arm Compiler 6
cmake_minimum_required(VERSION 3.20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(POLICY CMP0123)
    cmake_policy(SET CMP0123 NEW)
endif()

set(PROJ_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
project(M55M1_GestureAI VERSION 1.0.0 LANGUAGES C CXX ASM)

set(M55M1_DEBUG_UART_IDX 1 CACHE STRING "Debug UART index (0, 1, 5, or 6)")
set(M55_CPU "cortex-m55" CACHE STRING "-mcpu value")
set(M55M1_STACK_SIZE "0x2000" CACHE STRING "Stack size in bytes")
set(M55M1_HEAP_SIZE "" CACHE STRING "Heap size override, e.g. 0x1000")
option(M55M1_ENABLE_DSP "Build and link CMSIS-DSP" ON)
option(M55M1_ENABLE_NN "Build and link CMSIS-NN (requires DSP)" ON)
option(M55M1_ENABLE_RTOS2 "Build and link CMSIS-RTOS2/RTX" OFF)
option(M55M1_ENABLE_TFLM "Build and link TensorFlow Lite Micro with Ethos-U" OFF)
option(M55M1_ENABLE_DCACHE "Enable M55M1 data cache maintenance" ON)
option(M55M1_ENABLE_DAC_I2S_OUTPUT "Initialize TLV320DAC3100, I2S0, and playback DMA" ON)
set(M55M1_DMIC_GAIN_DB 30 CACHE STRING "DMIC hardware DSP gain in dB")

include(${PROJ_ROOT}/cmake/model_package.cmake)

set(M55_CPU "${M55_CPU}" CACHE STRING "-mcpu value" FORCE)

file(GLOB_RECURSE M55M1_STDDRIVER_HEADERS CONFIGURE_DEPENDS
    "${PROJ_ROOT}/libs/StdDriver/inc/*.h")
set(M55M1_STDDRIVER_INC_DIRS)
foreach(M55M1_HEADER IN LISTS M55M1_STDDRIVER_HEADERS)
    get_filename_component(M55M1_HEADER_DIR "${M55M1_HEADER}" DIRECTORY)
    list(APPEND M55M1_STDDRIVER_INC_DIRS "${M55M1_HEADER_DIR}")
endforeach()
list(REMOVE_DUPLICATES M55M1_STDDRIVER_INC_DIRS)

set(M55M1_INC_DIRS
    ${PROJ_ROOT}
    ${PROJ_ROOT}/libs/CMSIS/Core/Include
    ${PROJ_ROOT}/libs/Device/Nuvoton/M55M1/Include
    ${M55M1_STDDRIVER_INC_DIRS})

if(M55M1_ENABLE_DCACHE)
    set(M55M1_ENABLE_DCACHE_VALUE 1)
else()
    set(M55M1_ENABLE_DCACHE_VALUE 0)
endif()

set(M55M1_DEFS
    NVT_DCACHE_ON=${M55M1_ENABLE_DCACHE_VALUE}
    DEBUG_PORT_UART_IDX=${M55M1_DEBUG_UART_IDX}
    AUDIO_TRANSPORT_MIC_GAIN_DB=${M55M1_DMIC_GAIN_DB}
    ETHOSU_LOG_SEVERITY=-1)

set(M55M1_DEVICE_SOURCES
    ${PROJ_ROOT}/libs/Device/Nuvoton/M55M1/Source/startup_M55M1.c
    ${PROJ_ROOT}/libs/Device/Nuvoton/M55M1/Source/system_M55M1.c)
set_source_files_properties(
    ${PROJ_ROOT}/libs/Device/Nuvoton/M55M1/Source/system_M55M1.c
    PROPERTIES COMPILE_DEFINITIONS DEBUG_ENABLE_SEMIHOST=0)

file(GLOB_RECURSE M55M1_STDDRIVER_SOURCES CONFIGURE_DEPENDS
    "${PROJ_ROOT}/libs/StdDriver/src/*.c")
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
    target_include_directories(CMSISNN PUBLIC
        ${NN_ROOT}/Include ${NN_ROOT}/Include/Internal)
    target_link_libraries(CMSISNN PUBLIC CMSISDSP)
endif()

if(M55M1_ENABLE_TFLM)
    if(NOT M55M1_ENABLE_NN)
        message(FATAL_ERROR "M55M1_ENABLE_TFLM=ON requires M55M1_ENABLE_NN=ON")
    endif()
    include(${PROJ_ROOT}/cmake/tensorflow_lite_micro.cmake)
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

set(M55M1_APP_SOURCES
    ${PROJ_ROOT}/app/main.c
    ${PROJ_ROOT}/board/m55m1_audio.c
    ${PROJ_ROOT}/board/m55m1_timebase.c
    ${PROJ_ROOT}/board/m55m1_stack_monitor.c
    ${PROJ_ROOT}/drivers/audio_transport/audio_transport.c
    ${PROJ_ROOT}/services/audio_pipeline/audio_pipeline.c
    ${PROJ_ROOT}/services/enhancement_passthrough/enhancement_passthrough.c)

if(M55M1_ENABLE_DAC_I2S_OUTPUT)
    list(APPEND M55M1_APP_SOURCES
        ${PROJ_ROOT}/drivers/tlv320dac3100/tlv320dac3100.c)
    set(M55M1_ENABLE_DAC_I2S_OUTPUT_VALUE 1)
else()
    set(M55M1_ENABLE_DAC_I2S_OUTPUT_VALUE 0)
endif()

list(APPEND M55M1_APP_SOURCES ${M55M1_MODEL_SOURCES})

set(ELF_NAME M55M1_GestureAI.axf)
add_executable(${ELF_NAME}
    ${M55M1_DEVICE_SOURCES}
    ${M55M1_STDDRIVER_SOURCES}
    ${M55M1_APP_SOURCES})

target_compile_definitions(${ELF_NAME} PRIVATE
    ${M55M1_DEFS}
    M55M1_ENABLE_DAC_I2S_OUTPUT=${M55M1_ENABLE_DAC_I2S_OUTPUT_VALUE}
    M55M1_STACK_SIZE_BYTES=${M55M1_STACK_SIZE}
    ${M55M1_MODEL_DEFINITIONS})

set_target_properties(${ELF_NAME} PROPERTIES SUFFIX "")
target_include_directories(${ELF_NAME} PRIVATE
    ${M55M1_INC_DIRS}
    ${M55M1_MODEL_INCLUDE_DIRS})

if(M55M1_ENABLE_TFLM)
    target_link_libraries(${ELF_NAME} PRIVATE TensorFlowLiteMicro)
elseif(M55M1_ENABLE_NN)
    target_link_libraries(${ELF_NAME} PRIVATE CMSISNN)
elseif(M55M1_ENABLE_DSP)
    target_link_libraries(${ELF_NAME} PRIVATE CMSISDSP)
endif()
if(M55M1_ENABLE_RTOS2)
    target_link_libraries(${ELF_NAME} PRIVATE CMSISRTOS2)
endif()

if(M55M1_STACK_SIZE)
    target_link_options(${ELF_NAME} PRIVATE
        "--predefine=\"-DSTACK_SIZE=${M55M1_STACK_SIZE}\"")
endif()
if(M55M1_HEAP_SIZE)
    target_link_options(${ELF_NAME} PRIVATE
        "--predefine=\"-DHEAP_SIZE=${M55M1_HEAP_SIZE}\"")
endif()

add_linker_script(${ELF_NAME}
    "${PROJ_ROOT}/cmake/m55m1_audio.scf")
add_target_map_file(${ELF_NAME} "${CMAKE_BINARY_DIR}/${ELF_NAME}.map")
add_bin_generation_command(
    TARGET ${ELF_NAME}
    OUTPUT_BIN "${CMAKE_BINARY_DIR}/${ELF_NAME}.bin")

message(STATUS "M55M1_GestureAI: model package=${M55M1_MODEL_PACKAGE_STATUS}, DAC/I2S output=${M55M1_ENABLE_DAC_I2S_OUTPUT}, TensorFlow Lite Micro=${M55M1_ENABLE_TFLM}")
