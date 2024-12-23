# Copyright MediaZ Teknoloji A.S. All Rights Reserved.
cmake_minimum_required(VERSION 3.24.2)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CXX_STANDARD 20)

if (NOT WITH_NODOS_WORKSPACE)
    message(FATAL_ERROR "This repo currently does not support builds without Nodos workspace. "
    "Place this repo under nodos-workspace/Module folder and run cmake -S ./Toolchain/CMake -B Build from workspace root.")
endif()

# Nodos SDK
nos_find_sdk("1.3.0" NOS_PLUGIN_SDK_TARGET NOS_SUBSYSTEM_SDK_TARGET NOS_SDK_DIR)
set(FLATC_EXECUTABLE ${NOS_SDK_DIR}/bin/flatc${CMAKE_EXECUTABLE_SUFFIX})

# nos.device.decklink
nos_get_module("nos.sys.decklink" "1.0" NOS_SYS_DECKLINK_TARGET)

# nos.sys.mediaio: TODO: Add transitive dependency support to nosman or CMake Nodos toolchain
nos_get_module("nos.sys.mediaio" "0.2" NOS_SYS_MEDIAIO_TARGET)

# nos.sys.vulkan
nos_get_module("nos.sys.vulkan" "5.8" NOS_SYS_VULKAN_TARGET_5_8)

# nos.mediaio
nos_get_module("nos.mediaio" "2.0" NOS_MEDIAIO_TARGET)
nos_find_module_path("nos.mediaio" "2.0" NOS_MEDIAIO_PATH)
nos_generate_flatbuffers("${NOS_MEDIAIO_PATH}/Config" "${CMAKE_CURRENT_SOURCE_DIR}/Source/Generated" "cpp" "${NOS_MEDIAIO_PATH};${NOS_SDK_DIR}/types" generated_nosDeckLink_dep_nosMediaIO)

# nos.utilities
nos_get_module("nos.utilities" "2.9" NOS_UTILITIES_TARGET)
nos_find_module_path("nos.utilities" "2.9" NOS_UTILITIES_PATH)
nos_generate_flatbuffers("${NOS_UTILITIES_PATH}/Config" "${CMAKE_CURRENT_SOURCE_DIR}/Source/Generated" "cpp" "${NOS_UTILITIES_PATH};${NOS_SDK_DIR}/types" generated_nosDeckLink_dep_nosUtilities)

nos_generate_flatbuffers("${CMAKE_CURRENT_SOURCE_DIR}/Config" "${CMAKE_CURRENT_SOURCE_DIR}/Source/Generated" "cpp" "${NOS_SDK_DIR}/types" generated_nosDeckLink)

list(APPEND DEPENDENCIES generated_nosDeckLink_dep_nosMediaIO generated_nosDeckLink_dep_nosUtilities generated_nosDeckLink
    ${NOS_SYS_DECKLINK_TARGET} ${NOS_SYS_MEDIAIO_TARGET} ${NOS_SYS_VULKAN_TARGET_5_8} ${NOS_PLUGIN_SDK_TARGET})
list(APPEND INCLUDE_FOLDERS
    ${EXTERNAL_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_BINARY_DIR})

nos_add_plugin("nosDeckLink" "${DEPENDENCIES}" "${INCLUDE_FOLDERS}")

# Project generation
nos_group_targets("nosDeckLink" "NOS Plugins")