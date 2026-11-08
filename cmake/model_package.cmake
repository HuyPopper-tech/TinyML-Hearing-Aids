# The checked-in DeepFIR package is the only model selected by the product build.

set(M55M1_MODEL_PACKAGE_DIR "${PROJ_ROOT}/models/deepfir" CACHE PATH
    "Project-owned DeepFIR model package directory")
if("${M55M1_MODEL_PACKAGE_DIR}" STREQUAL "")
    set(M55M1_MODEL_PACKAGE_DIR "${PROJ_ROOT}/models/deepfir" CACHE PATH
        "Project-owned DeepFIR model package directory" FORCE)
endif()
set(M55M1_MODEL_PACKAGE_STATUS "NONE")

get_filename_component(M55M1_MODEL_PACKAGE_DIR
    "${M55M1_MODEL_PACKAGE_DIR}" ABSOLUTE BASE_DIR "${PROJ_ROOT}")
if(NOT IS_DIRECTORY "${M55M1_MODEL_PACKAGE_DIR}")
    message(FATAL_ERROR
        "M55M1_MODEL_PACKAGE_DIR is not a directory: ${M55M1_MODEL_PACKAGE_DIR}")
endif()

foreach(M55M1_REQUIRED_FILE IN ITEMS integration_manifest.json m55m1_model_package.cmake)
    if(NOT EXISTS "${M55M1_MODEL_PACKAGE_DIR}/${M55M1_REQUIRED_FILE}" OR
       IS_DIRECTORY "${M55M1_MODEL_PACKAGE_DIR}/${M55M1_REQUIRED_FILE}")
        message(FATAL_ERROR
            "Model package is missing ${M55M1_REQUIRED_FILE}: ${M55M1_MODEL_PACKAGE_DIR}")
    endif()
endforeach()

include("${M55M1_MODEL_PACKAGE_DIR}/m55m1_model_package.cmake")

foreach(M55M1_REQUIRED_VARIABLE IN ITEMS
        M55M1_MODEL_SOURCES M55M1_MODEL_INCLUDE_DIRS M55M1_MODEL_DEFINITIONS)
    if(NOT DEFINED ${M55M1_REQUIRED_VARIABLE})
        message(FATAL_ERROR
            "Model package must define ${M55M1_REQUIRED_VARIABLE}")
    endif()
endforeach()
if("${M55M1_MODEL_SOURCES}" STREQUAL "")
    message(FATAL_ERROR "Model package must define at least one source")
endif()

set(M55M1_MODEL_SOURCE_LIST ${M55M1_MODEL_SOURCES})
set(M55M1_MODEL_INCLUDE_LIST ${M55M1_MODEL_INCLUDE_DIRS})
set(M55M1_MODEL_SOURCES)
set(M55M1_MODEL_INCLUDE_DIRS)

function(m55m1_model_package_path_allowed path label)
    get_filename_component(M55M1_RESOLVED_PATH "${path}" ABSOLUTE)
    get_filename_component(M55M1_PACKAGE_ROOT "${M55M1_MODEL_PACKAGE_DIR}" ABSOLUTE)
    get_filename_component(M55M1_SERVICES_ROOT "${PROJ_ROOT}/services" ABSOLUTE)
    file(TO_CMAKE_PATH "${M55M1_RESOLVED_PATH}" M55M1_RESOLVED_PATH)
    file(TO_CMAKE_PATH "${M55M1_PACKAGE_ROOT}" M55M1_PACKAGE_ROOT)
    file(TO_CMAKE_PATH "${M55M1_SERVICES_ROOT}" M55M1_SERVICES_ROOT)
    set(M55M1_ALLOWED FALSE)
    foreach(M55M1_ROOT IN ITEMS "${M55M1_PACKAGE_ROOT}" "${M55M1_SERVICES_ROOT}")
        string(LENGTH "${M55M1_ROOT}/" M55M1_ROOT_LENGTH)
        string(LENGTH "${M55M1_RESOLVED_PATH}" M55M1_PATH_LENGTH)
        if(M55M1_RESOLVED_PATH STREQUAL M55M1_ROOT OR
           M55M1_PATH_LENGTH GREATER_EQUAL M55M1_ROOT_LENGTH)
            if(M55M1_RESOLVED_PATH STREQUAL M55M1_ROOT)
                set(M55M1_ALLOWED TRUE)
            else()
                string(SUBSTRING "${M55M1_RESOLVED_PATH}" 0 ${M55M1_ROOT_LENGTH}
                    M55M1_ROOT_PREFIX)
                if(M55M1_ROOT_PREFIX STREQUAL "${M55M1_ROOT}/")
                    set(M55M1_ALLOWED TRUE)
                endif()
            endif()
        endif()
    endforeach()
    if(NOT M55M1_ALLOWED)
        message(FATAL_ERROR
            "Model package ${label} escapes package/services roots: ${path}")
    endif()
    set(${label} "${M55M1_RESOLVED_PATH}" PARENT_SCOPE)
endfunction()

function(m55m1_model_package_source_allowed path)
    get_filename_component(M55M1_SOURCE_NAME "${path}" NAME)
    if(M55M1_SOURCE_NAME STREQUAL "ethosu_driver.c" OR
       M55M1_SOURCE_NAME STREQUAL "ethosu_device_u55_u65.c")
        message(FATAL_ERROR
            "Model package cannot provide duplicate Ethos-U core source: ${path}")
    endif()
    file(READ "${path}" M55M1_SOURCE_CONTENT)
    string(FIND "${M55M1_SOURCE_CONTENT}" "NPU_IRQHandler"
        M55M1_IRQ_SYMBOL_INDEX)
    if(NOT M55M1_IRQ_SYMBOL_INDEX EQUAL -1)
        message(FATAL_ERROR
            "Model package cannot define duplicate NPU_IRQHandler: ${path}")
    endif()
endfunction()

foreach(M55M1_MODEL_SOURCE IN LISTS M55M1_MODEL_SOURCE_LIST)
    if(IS_ABSOLUTE "${M55M1_MODEL_SOURCE}")
        set(M55M1_SOURCE_PATH "${M55M1_MODEL_SOURCE}")
    else()
        set(M55M1_SOURCE_PATH "${M55M1_MODEL_PACKAGE_DIR}/${M55M1_MODEL_SOURCE}")
    endif()
    m55m1_model_package_path_allowed("${M55M1_SOURCE_PATH}" M55M1_SOURCE_PATH)
    if(NOT EXISTS "${M55M1_SOURCE_PATH}" OR IS_DIRECTORY "${M55M1_SOURCE_PATH}")
        message(FATAL_ERROR "Model package source is missing: ${M55M1_SOURCE_PATH}")
    endif()
    m55m1_model_package_source_allowed("${M55M1_SOURCE_PATH}")
    list(APPEND M55M1_MODEL_SOURCES "${M55M1_SOURCE_PATH}")
endforeach()

foreach(M55M1_MODEL_INCLUDE IN LISTS M55M1_MODEL_INCLUDE_LIST)
    if(IS_ABSOLUTE "${M55M1_MODEL_INCLUDE}")
        set(M55M1_INCLUDE_PATH "${M55M1_MODEL_INCLUDE}")
    else()
        set(M55M1_INCLUDE_PATH "${M55M1_MODEL_PACKAGE_DIR}/${M55M1_MODEL_INCLUDE}")
    endif()
    m55m1_model_package_path_allowed("${M55M1_INCLUDE_PATH}" M55M1_INCLUDE_PATH)
    if(NOT IS_DIRECTORY "${M55M1_INCLUDE_PATH}")
        message(FATAL_ERROR "Model package include directory is missing: ${M55M1_INCLUDE_PATH}")
    endif()
    list(APPEND M55M1_MODEL_INCLUDE_DIRS "${M55M1_INCLUDE_PATH}")
endforeach()

set(M55M1_MODEL_DEFINITIONS ${M55M1_MODEL_DEFINITIONS})
set(M55M1_MODEL_PACKAGE_STATUS "${M55M1_MODEL_PACKAGE_DIR}")
message(STATUS "Model package: ${M55M1_MODEL_PACKAGE_DIR}")
