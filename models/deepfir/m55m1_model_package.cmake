# DeepFIR project-local product package integration.
# Ethos-U core/IRQ remain firmware-owned.

set(M55M1_MODEL_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/deepfir_model_data.cc
    ${PROJ_ROOT}/services/deepfir/deepfir_dsp.c
    ${PROJ_ROOT}/services/deepfir/deepfir_features.c
    ${PROJ_ROOT}/services/deepfir/deepfir_model_adapter.c
    ${PROJ_ROOT}/services/deepfir/deepfir_reconstruction.c
    ${PROJ_ROOT}/services/deepfir/deepfir_tensor_binding.c
    ${PROJ_ROOT}/services/deepfir/deepfir_cmsis_fft_adapter.c
    ${PROJ_ROOT}/services/deepfir/deepfir_model_package.cc
    ${PROJ_ROOT}/services/deepfir/deepfir_inference_service.cpp)
set(M55M1_MODEL_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}
    ${PROJ_ROOT}/services/deepfir)
set(M55M1_MODEL_DEFINITIONS
    DEEPFIR_MODEL_PACKAGE=1
    DEEPFIR_DSP_CMSIS_FFT=1)
