# TensorFlow Lite Micro source-only runtime for the M55M1 Ethos-U55 platform.

set(TFLM_ROOT "${PROJ_ROOT}/libs/tensorflow")
set(TFLM_RUNTIME_ROOT "${TFLM_ROOT}/runtime")
set(TFLM_THIRD_PARTY_ROOT "${TFLM_ROOT}/third_party")

foreach(TFLM_REQUIRED_PATH IN ITEMS
    "${TFLM_RUNTIME_ROOT}/tensorflow/lite/micro/micro_interpreter.h"
    "${TFLM_RUNTIME_ROOT}/tensorflow/lite/micro/kernels/ethos_u/ethosu.cpp"
    "${TFLM_THIRD_PARTY_ROOT}/flatbuffers/include/flatbuffers/flexbuffers.h"
    "${TFLM_THIRD_PARTY_ROOT}/gemmlowp/fixedpoint/fixedpoint.h"
    "${TFLM_THIRD_PARTY_ROOT}/kissfft/kiss_fft.c"
    "${TFLM_THIRD_PARTY_ROOT}/kissfft/tools/kiss_fftr.c")
    if(NOT EXISTS "${TFLM_REQUIRED_PATH}")
        message(FATAL_ERROR "TensorFlow Lite Micro source is incomplete: ${TFLM_REQUIRED_PATH}")
    endif()
endforeach()

file(GLOB_RECURSE M55M1_TFLM_SOURCES CONFIGURE_DEPENDS
    "${TFLM_RUNTIME_ROOT}/tensorflow/*.c"
    "${TFLM_RUNTIME_ROOT}/tensorflow/*.cc"
    "${TFLM_RUNTIME_ROOT}/tensorflow/*.cpp"
    "${TFLM_RUNTIME_ROOT}/signal/*.c"
    "${TFLM_RUNTIME_ROOT}/signal/*.cc"
    "${TFLM_RUNTIME_ROOT}/signal/*.cpp")

# The retained bundle is source-only: keep runtime/kernel sources and reject
# any test, mock, or legacy Ethos-U implementation that reaches the glob.
list(FILTER M55M1_TFLM_SOURCES EXCLUDE REGEX "[/\\\\](testing|examples)[/\\\\]")
list(FILTER M55M1_TFLM_SOURCES EXCLUDE REGEX "[/\\\\](mock_|test_).*")
list(FILTER M55M1_TFLM_SOURCES EXCLUDE REGEX "_test\\.(c|cc|cpp)$")

# CMSIS-NN provides alternate implementations of the generic TFLM kernels.
# Select each optimized provider and remove its generic source with the same
# filename so their Register_* symbols cannot be linked twice.
file(GLOB M55M1_TFLM_CMSIS_NN_KERNEL_SOURCES CONFIGURE_DEPENDS
    "${TFLM_RUNTIME_ROOT}/tensorflow/lite/micro/kernels/cmsis_nn/*.cpp")
foreach(M55M1_TFLM_CMSIS_NN_SOURCE IN LISTS M55M1_TFLM_CMSIS_NN_KERNEL_SOURCES)
    get_filename_component(M55M1_TFLM_CMSIS_NN_FILENAME
        "${M55M1_TFLM_CMSIS_NN_SOURCE}" NAME)
    list(REMOVE_ITEM M55M1_TFLM_SOURCES
        "${TFLM_RUNTIME_ROOT}/tensorflow/lite/micro/kernels/${M55M1_TFLM_CMSIS_NN_FILENAME}")
endforeach()
list(REMOVE_ITEM M55M1_TFLM_SOURCES
    "${TFLM_RUNTIME_ROOT}/tensorflow/lite/micro/kernels/ethosu.cpp"
    "${TFLM_RUNTIME_ROOT}/tensorflow/lite/micro/cortex_m_generic/micro_time.cpp")

list(APPEND M55M1_TFLM_SOURCES
    "${TFLM_THIRD_PARTY_ROOT}/kissfft/kiss_fft.c"
    "${TFLM_THIRD_PARTY_ROOT}/kissfft/tools/kiss_fftr.c")

# KissFFT headers intentionally declare C++ linkage when included by TFLM and
# the ERB-GRU service, so compile its implementation with the same linkage.
set_source_files_properties(
    "${TFLM_THIRD_PARTY_ROOT}/kissfft/kiss_fft.c"
    "${TFLM_THIRD_PARTY_ROOT}/kissfft/tools/kiss_fftr.c"
    PROPERTIES LANGUAGE CXX)

add_library(TensorFlowLiteMicro STATIC ${M55M1_TFLM_SOURCES})
set_property(TARGET TensorFlowLiteMicro PROPERTY CXX_STANDARD 11)
target_include_directories(TensorFlowLiteMicro PUBLIC
    "${TFLM_RUNTIME_ROOT}"
    "${TFLM_THIRD_PARTY_ROOT}/flatbuffers/include"
    "${TFLM_THIRD_PARTY_ROOT}/gemmlowp"
    "${TFLM_THIRD_PARTY_ROOT}/kissfft"
    "${TFLM_THIRD_PARTY_ROOT}/ruy")
target_include_directories(TensorFlowLiteMicro PRIVATE ${M55M1_STDDRIVER_INC_DIRS})
target_compile_definitions(TensorFlowLiteMicro PUBLIC
    TF_LITE_STATIC_MEMORY=1
    TF_LITE_DISABLE_X86_NEON=1
    CMSIS_NN=1)
target_link_libraries(TensorFlowLiteMicro PUBLIC CMSISNN)

message(STATUS "TensorFlow Lite Micro: ${TFLM_RUNTIME_ROOT}")
