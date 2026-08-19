# Copyright MediaZ Teknoloji A.S. All Rights Reserved.
cmake_minimum_required(VERSION 3.24.2)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CXX_STANDARD 20)

if (NOT WITH_NODOS_WORKSPACE)
    message(FATAL_ERROR "This repo currently does not support builds without Nodos workspace. "
    "Place this repo under nodos-workspace/Module folder and run cmake -S ./Toolchain/CMake -B Build from workspace root.")
endif()

add_compile_definitions(NOS_DISABLE_DEPRECATED)
# Nodos SDK
nos_find_sdk("1.3.0" NOS_PLUGIN_SDK_TARGET NOS_SUBSYSTEM_SDK_TARGET NOS_SDK_DIR)
set(FLATC_EXECUTABLE ${NOS_SDK_DIR}/bin/flatc${CMAKE_EXECUTABLE_SUFFIX})

set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source")
# Each dependency generates into its own folder. Two modules can ship schemas with the
# same file name, and flatc names its output after the schema, so a shared folder makes
# them collide. nos.utilities and nos.mediaio both ship ChannelViewer.fbs.
set(GENERATED_DIR "${SOURCE_DIR}/__generated__")

# nos.sys.decklink
nos_get_module("nos.sys.decklink" "2.4" NOS_SYS_DECKLINK_TARGET)

# nos.sys.mediaio: TODO: Add transitive dependency support to nosman or CMake Nodos toolchain
nos_get_module("nos.sys.mediaio" "0.4" NOS_SYS_MEDIAIO_TARGET)

# nos.sys.vulkan
nos_get_module("nos.sys.vulkan" "6.0" NOS_SYS_VULKAN_TARGET)

# nos.mediaio
nos_get_module("nos.mediaio" "2.13" NOS_MEDIAIO_TARGET)
nos_find_module_path("nos.mediaio" "2.13" NOS_MEDIAIO_PATH)
set(NOS_MEDIAIO_GENERATED_DIR "${GENERATED_DIR}/nos.mediaio")
nos_generate_flatbuffers("${NOS_MEDIAIO_PATH}/Config" "${NOS_MEDIAIO_GENERATED_DIR}" "cpp" "${NOS_MEDIAIO_PATH};${NOS_SDK_DIR}/types" generated_nosDeckLink_dep_nosMediaIO)

# nos.utilities
nos_get_module("nos.utilities" "3.8" NOS_UTILITIES_TARGET)
nos_find_module_path("nos.utilities" "3.8" NOS_UTILITIES_PATH)
set(NOS_UTILITIES_GENERATED_DIR "${GENERATED_DIR}/nos.utilities")
nos_generate_flatbuffers("${NOS_UTILITIES_PATH}/Config" "${NOS_UTILITIES_GENERATED_DIR}" "cpp" "${NOS_UTILITIES_PATH};${NOS_SDK_DIR}/types" generated_nosDeckLink_dep_nosUtilities)

# nos.sys.device
nos_get_module("nos.sys.device" "0.3" NOS_SYS_DEVICE_TARGET)
nos_find_module_path("nos.sys.device" "0.3" NOS_SYS_DEVICE_PATH)
set(NOS_SYS_DEVICE_GENERATED_DIR "${GENERATED_DIR}/nos.sys.device")
nos_generate_flatbuffers("${NOS_SYS_DEVICE_PATH}/Config" "${NOS_SYS_DEVICE_GENERATED_DIR}" "cpp" "${NOS_SYS_DEVICE_PATH};${NOS_SDK_DIR}/types" generated_nosDeckLink_dep_nosSysDevice)

# nos.sync
nos_get_module("nos.sync" "3.2" NOS_SYNC_TARGET)

# TODO: Publish as nosman package
add_subdirectory(External/cpp-utilities)

nos_generate_flatbuffers("${CMAKE_CURRENT_SOURCE_DIR}/Config" "${GENERATED_DIR}" "cpp" "${NOS_SDK_DIR}/types" generated_nosDeckLink)

list(APPEND DEPENDENCIES generated_nosDeckLink_dep_nosMediaIO generated_nosDeckLink_dep_nosUtilities generated_nosDeckLink_dep_nosSysDevice nosCppUtilities
    generated_nosDeckLink ${NOS_SYS_DEVICE_TARGET} ${NOS_SYS_DECKLINK_TARGET} ${NOS_SYS_MEDIAIO_TARGET} ${NOS_SYS_VULKAN_TARGET} ${NOS_PLUGIN_SDK_TARGET} ${NOS_SYNC_TARGET})
list(APPEND INCLUDE_FOLDERS
    ${EXTERNAL_DIR}
    ${SOURCE_DIR}
    ${GENERATED_DIR}
    ${NOS_MEDIAIO_GENERATED_DIR}
    ${NOS_UTILITIES_GENERATED_DIR}
    ${NOS_SYS_DEVICE_GENERATED_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})

nos_add_plugin("nosDeckLink" "${DEPENDENCIES}" "${INCLUDE_FOLDERS}")

# Project generation
nos_group_targets("nosDeckLink" "NOS Plugins")