# Custom hearing-aid PCB M55M1 (M55M1R2LJAE) / Arm Compiler 6
cmake_minimum_required(VERSION 3.20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(POLICY CMP0123)
    cmake_policy(SET CMP0123 NEW)
endif()

set(PROJ_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
project(M55M1_GestureAI VERSION 1.0.0 LANGUAGES C CXX ASM)

set(M55M1_DEBUG_UART_IDX 2 CACHE STRING "Custom PCB debug UART (PC4 RX / PC5 TX)")
if(NOT M55M1_DEBUG_UART_IDX STREQUAL "2")
    message(FATAL_ERROR "The custom PCB requires UART2; UART1 conflicts with DMIC PA2")
endif()
set(M55_CPU "cortex-m55" CACHE STRING "-mcpu value")
set(M55M1_STACK_SIZE "0x2000" CACHE STRING "Stack size in bytes")
set(M55M1_HEAP_SIZE "" CACHE STRING "Heap size override, e.g. 0x1000")
option(M55M1_ENABLE_DSP "Build and link CMSIS-DSP" ON)
option(M55M1_ENABLE_NN "Build and link CMSIS-NN (requires DSP)" ON)
option(M55M1_ENABLE_ETHOSU_RUNTIME "Build the Ethos-U runtime" ON)
option(M55M1_ENABLE_TFLM "Build and link TensorFlow Lite Micro with Ethos-U" ON)
option(M55M1_ENABLE_DCACHE "Enable M55M1 data cache maintenance" ON)
option(M55M1_ENABLE_DAC_I2S_OUTPUT "Initialize TLV320DAC3100, I2S1, and playback DMA" ON)
option(M55M1_AUDIO_RELEASE_PROBE "Finite Release audio timing probe; UART only after DMA stops" OFF)
option(M55M1_AUDIO_UART_PCM_CAPTURE "One-shot paired DeepFIR PCM capture over UART" OFF)
option(M55M1_DEEPFIR_COMPILED_REPLAY "Foreground debugger mailbox replay; audio stays stopped" OFF)
if(M55M1_DEEPFIR_COMPILED_REPLAY AND (M55M1_AUDIO_UART_PCM_CAPTURE OR M55M1_AUDIO_RELEASE_PROBE))
    message(FATAL_ERROR "Compiled replay and audio diagnostics are mutually exclusive")
endif()
option(M55M1_DEEPFIR_MVE "Use Cortex-M55 Helium kernels on the DeepFIR DSP path" ON)
if(M55M1_AUDIO_RELEASE_PROBE)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT M55M1_ENABLE_DAC_I2S_OUTPUT)
        message(FATAL_ERROR "M55M1_AUDIO_RELEASE_PROBE requires Release and DAC/I2S output")
    endif()
    set(M55M1_AUDIO_RELEASE_PROBE_VALUE 1)
else()
    set(M55M1_AUDIO_RELEASE_PROBE_VALUE 0)
endif()
if(M55M1_AUDIO_UART_PCM_CAPTURE)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR
       NOT M55M1_ENABLE_DAC_I2S_OUTPUT OR
       NOT M55M1_ENABLE_ETHOSU_RUNTIME OR
       NOT M55M1_ENABLE_TFLM OR
       M55M1_AUDIO_RELEASE_PROBE OR
       NOT M55M1_DEBUG_UART_IDX STREQUAL "2")
        message(FATAL_ERROR
            "M55M1_AUDIO_UART_PCM_CAPTURE requires Release, UART2, DAC/I2S, Ethos-U/TFLM, and AUDIO_RELEASE_PROBE=OFF")
    endif()
    set(M55M1_AUDIO_UART_PCM_CAPTURE_VALUE 1)
else()
    set(M55M1_AUDIO_UART_PCM_CAPTURE_VALUE 0)
endif()
option(M55M1_MODEL_IN_SRAM "Place the model image in SRAM at runtime" ON)

include(${PROJ_ROOT}/cmake/model_package.cmake)

set(M55M1_DMIC_GAIN_DB 30 CACHE STRING "DMIC hardware DSP gain in dB")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(M55M1_RUNTIME_TELEMETRY_VALUE 1)
else()
    set(M55M1_RUNTIME_TELEMETRY_VALUE 0)
endif()

set(M55M1_DEEPFIR_MVE_SELF_TEST_VALUE 1)

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
    M55M1_DMIC_GAIN_DB=${M55M1_DMIC_GAIN_DB}
    M55M1_RUNTIME_TELEMETRY=${M55M1_RUNTIME_TELEMETRY_VALUE}
    ETHOSU_LOG_SEVERITY=-1)

set(M55M1_DEVICE_SOURCES
    ${PROJ_ROOT}/libs/Device/Nuvoton/M55M1/Source/startup_M55M1.c
    ${PROJ_ROOT}/libs/Device/Nuvoton/M55M1/Source/system_M55M1.c)
set_source_files_properties(
    ${PROJ_ROOT}/libs/Device/Nuvoton/M55M1/Source/system_M55M1.c
    PROPERTIES COMPILE_DEFINITIONS DEBUG_ENABLE_SEMIHOST=0)

file(GLOB_RECURSE M55M1_STDDRIVER_SOURCES CONFIGURE_DEPENDS
    "${PROJ_ROOT}/libs/StdDriver/src/*.c")
file(GLOB M55M1_ETHOSU_SOURCES CONFIGURE_DEPENDS
    "${PROJ_ROOT}/libs/StdDriver/src/npu/*.c")
set(M55M1_ETHOSU_CORE_SOURCES)
list(APPEND M55M1_ETHOSU_CORE_SOURCES
    "${PROJ_ROOT}/libs/StdDriver/src/npu/ethosu_driver.c"
    "${PROJ_ROOT}/libs/StdDriver/src/npu/ethosu_device_u55_u65.c")
list(REMOVE_ITEM M55M1_STDDRIVER_SOURCES ${M55M1_ETHOSU_SOURCES})
if(M55M1_ENABLE_ETHOSU_RUNTIME)
    list(APPEND M55M1_STDDRIVER_SOURCES ${M55M1_ETHOSU_CORE_SOURCES})
endif()
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
    if(NOT M55M1_ENABLE_ETHOSU_RUNTIME)
        message(FATAL_ERROR "M55M1_ENABLE_TFLM=ON requires M55M1_ENABLE_ETHOSU_RUNTIME=ON")
    endif()
    if(NOT M55M1_ENABLE_NN)
        message(FATAL_ERROR "M55M1_ENABLE_TFLM=ON requires M55M1_ENABLE_NN=ON")
    endif()
    include(${PROJ_ROOT}/cmake/tensorflow_lite_micro.cmake)
endif()

set(M55M1_RUNTIME_SOURCES)
if(M55M1_ENABLE_ETHOSU_RUNTIME)
    list(APPEND M55M1_RUNTIME_SOURCES
        ${PROJ_ROOT}/board/m55m1_ethosu_board.c
        ${PROJ_ROOT}/drivers/ethosu_runtime/ethosu_runtime.c)
endif()
if(M55M1_ENABLE_TFLM)
    list(APPEND M55M1_RUNTIME_SOURCES
        ${PROJ_ROOT}/drivers/ethosu_runtime/ethosu_tflm_bridge.cpp)
endif()

set(M55M1_APP_SOURCES
    ${PROJ_ROOT}/app/main.c
    ${PROJ_ROOT}/board/m55m1_audio.c
    ${PROJ_ROOT}/board/m55m1_power.c
    ${PROJ_ROOT}/board/m55m1_timebase.c
    ${PROJ_ROOT}/board/m55m1_stack_monitor.c
    ${PROJ_ROOT}/drivers/audio_transport/audio_transport.c
    ${PROJ_ROOT}/services/audio_pipeline/audio_pipeline.c)

if(M55M1_ENABLE_DAC_I2S_OUTPUT)
    list(APPEND M55M1_APP_SOURCES
        ${PROJ_ROOT}/drivers/tlv320dac3100/tlv320dac3100.c)
    set(M55M1_ENABLE_DAC_I2S_OUTPUT_VALUE 1)
else()
    set(M55M1_ENABLE_DAC_I2S_OUTPUT_VALUE 0)
endif()

list(APPEND M55M1_APP_SOURCES ${M55M1_MODEL_SOURCES})
set_source_files_properties(
    ${PROJ_ROOT}/services/deepfir/deepfir_features.c
    ${PROJ_ROOT}/services/deepfir/deepfir_dsp.c
    ${PROJ_ROOT}/services/deepfir/deepfir_model_adapter.c
    ${PROJ_ROOT}/services/deepfir/deepfir_reconstruction.c
    ${PROJ_ROOT}/services/deepfir/deepfir_tensor_binding.c
    ${PROJ_ROOT}/services/deepfir/deepfir_inference_service.cpp
    PROPERTIES COMPILE_OPTIONS "-fno-finite-math-only")
if(M55M1_RUNTIME_TELEMETRY_VALUE OR M55M1_AUDIO_RELEASE_PROBE_VALUE OR
   M55M1_AUDIO_UART_PCM_CAPTURE_VALUE)
    set(M55M1_DEBUG_UART_ENABLED_VALUE 1)
    list(APPEND M55M1_APP_SOURCES
        ${PROJ_ROOT}/board/m55m1_debug_uart.c
        ${PROJ_ROOT}/drivers/debug_uart_tx/debug_uart_tx.c
        ${PROJ_ROOT}/common/fixed_byte_ring.c)
else()
    set(M55M1_DEBUG_UART_ENABLED_VALUE 0)
endif()
set(ELF_NAME M55M1_GestureAI.axf)
add_executable(${ELF_NAME}
    ${M55M1_DEVICE_SOURCES}
    ${M55M1_STDDRIVER_SOURCES}
    ${M55M1_RUNTIME_SOURCES}
    ${M55M1_APP_SOURCES})

target_compile_definitions(${ELF_NAME} PRIVATE
    ${M55M1_DEFS}
    NVT_DBG_UART_OFF=1
    M55M1_DEBUG_UART_ENABLED=${M55M1_DEBUG_UART_ENABLED_VALUE}
    M55M1_ENABLE_DAC_I2S_OUTPUT=${M55M1_ENABLE_DAC_I2S_OUTPUT_VALUE}
    M55M1_AUDIO_RELEASE_PROBE=${M55M1_AUDIO_RELEASE_PROBE_VALUE}
    M55M1_AUDIO_UART_PCM_CAPTURE=${M55M1_AUDIO_UART_PCM_CAPTURE_VALUE}
    M55M1_DEEPFIR_COMPILED_REPLAY=$<BOOL:${M55M1_DEEPFIR_COMPILED_REPLAY}>
    M55M1_DEEPFIR_MVE=$<BOOL:${M55M1_DEEPFIR_MVE}>
    M55M1_DEEPFIR_MVE_SELF_TEST=${M55M1_DEEPFIR_MVE_SELF_TEST_VALUE}
    M55M1_STACK_SIZE_BYTES=${M55M1_STACK_SIZE}
    ${M55M1_MODEL_DEFINITIONS})

target_compile_definitions(${ELF_NAME} PRIVATE M55M1_MODEL_IN_SRAM=1)
set_target_properties(${ELF_NAME} PROPERTIES SUFFIX "")
target_include_directories(${ELF_NAME} PRIVATE
    ${M55M1_INC_DIRS}
    ${M55M1_MODEL_INCLUDE_DIRS})

if(M55M1_ENABLE_TFLM)
    target_include_directories(${ELF_NAME} PRIVATE
        ${TFLM_RUNTIME_ROOT}
        ${TFLM_THIRD_PARTY_ROOT}/flatbuffers/include
        ${TFLM_THIRD_PARTY_ROOT}/gemmlowp
        ${TFLM_THIRD_PARTY_ROOT}/kissfft
        ${TFLM_THIRD_PARTY_ROOT}/ruy)
endif()

if(M55M1_ENABLE_TFLM)
    target_link_libraries(${ELF_NAME} PRIVATE TensorFlowLiteMicro)
elseif(M55M1_ENABLE_NN)
    target_link_libraries(${ELF_NAME} PRIVATE CMSISNN)
elseif(M55M1_ENABLE_DSP)
    target_link_libraries(${ELF_NAME} PRIVATE CMSISDSP)
endif()

if(M55M1_STACK_SIZE)
    target_link_options(${ELF_NAME} PRIVATE
        "--predefine=\"-DSTACK_SIZE=${M55M1_STACK_SIZE}\"")
endif()
if(M55M1_HEAP_SIZE)
    target_link_options(${ELF_NAME} PRIVATE
        "--predefine=\"-DHEAP_SIZE=${M55M1_HEAP_SIZE}\"")
endif()
if(M55M1_MODEL_IN_SRAM)
    target_link_options(${ELF_NAME} PRIVATE
        "--predefine=\"-DM55M1_MODEL_IN_SRAM=1\""
        )
endif()

add_linker_script(${ELF_NAME}
    "${PROJ_ROOT}/cmake/m55m1_audio.scf")
add_target_map_file(${ELF_NAME} "${CMAKE_BINARY_DIR}/${ELF_NAME}.map")
add_bin_generation_command(
    TARGET ${ELF_NAME}
    OUTPUT_BIN "${CMAKE_BINARY_DIR}/M55M1_GestureAI.bin")
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND M55M1_MODEL_IN_SRAM)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_target(model_sram_report ALL
        COMMAND ${Python3_EXECUTABLE} "${PROJ_ROOT}/tools/report_model_sram.py"
                "${CMAKE_BINARY_DIR}/${ELF_NAME}.map"
        DEPENDS ${ELF_NAME}
        VERBATIM)
endif()

message(STATUS "M55M1_GestureAI: model package=${M55M1_MODEL_PACKAGE_STATUS}, Ethos-U runtime=${M55M1_ENABLE_ETHOSU_RUNTIME}, DAC/I2S output=${M55M1_ENABLE_DAC_I2S_OUTPUT}, TensorFlow Lite Micro=${M55M1_ENABLE_TFLM}")
