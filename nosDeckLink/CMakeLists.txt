# Copyright MediaZ Teknoloji A.S. All Rights Reserved.

# TODO: Publish as nosman package
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/External/cpp-utilities ${CMAKE_CURRENT_BINARY_DIR}/External/cpp-utilities)
target_link_libraries(${NOS_PLUGIN_TARGET} PRIVATE nosCppUtilities)

nos_get_targets(NOSDECKLINK_EXTERNAL_TARGETS "${CMAKE_CURRENT_BINARY_DIR}/External/cpp-utilities")
nos_group_targets("${NOSDECKLINK_EXTERNAL_TARGETS}" "External")

nos_group_targets(${NOS_PLUGIN_TARGET} "NOS Plugins")