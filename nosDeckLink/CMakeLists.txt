# Copyright MediaZ Teknoloji A.S. All Rights Reserved.

# TODO: Publish as nosman package
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/External/cpp-utilities ${CMAKE_CURRENT_BINARY_DIR}/External/cpp-utilities)
target_link_libraries(${NOS_PLUGIN_TARGET} PRIVATE nosCppUtilities)
target_compile_definitions(${NOS_PLUGIN_TARGET} PRIVATE "NOS_DISABLE_DEPRECATED")

nos_get_targets(NOSDECKLINK_EXTERNAL_TARGETS "${CMAKE_CURRENT_BINARY_DIR}/External/cpp-utilities")
nos_group_targets("${NOSDECKLINK_EXTERNAL_TARGETS}" "External")