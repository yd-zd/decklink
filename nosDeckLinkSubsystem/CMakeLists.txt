# Copyright MediaZ Teknoloji A.S. All Rights Reserved.

# DeckLink SDK
# ------------
set(DECKLINK_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/External/DeckLinkSDK")

if (WIN32)
    set(COMPILED_DECKLINK_FOLDER ${CMAKE_CURRENT_BINARY_DIR}/External/DeckLink)
    # Add DeckLink API
    set(DECKLINK_SOURCES
            ${COMPILED_DECKLINK_FOLDER}/DeckLinkAPI_i.c
            ${COMPILED_DECKLINK_FOLDER}/DeckLinkAPI.h)

    set(DECKLINK_SDK_INCLUDE_DIR ${DECKLINK_SDK_DIR}/Win/include)

    # Link COM support
    list(APPEND PLATFORM_LIBRARIES comsuppw.lib)

    # Compile DeckLink API IDL to build directory
    set(MIDL_OUTPUT_SOURCE "${COMPILED_DECKLINK_FOLDER}\\DeckLinkAPI_i.c")
    set(MIDL_OUTPUT_HEADER "${COMPILED_DECKLINK_FOLDER}\\DeckLinkAPI.h")
    set(MIDL_OUTPUT_TLB "${COMPILED_DECKLINK_FOLDER}\\DeckLinkAPI.tlb")
    set(DECKLINK_API_INTEROP "${COMPILED_DECKLINK_FOLDER}\\Interop.DeckLinkAPI.dll")
    set(DECKLINK_API_IDL "${DECKLINK_SDK_INCLUDE_DIR}\\DeckLinkAPI.idl")
    set(DECKLINK_SDK_INCLUDE_DIR ${COMPILED_DECKLINK_FOLDER})

    if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)")
        set(MIDL_MACHINE win32)
    else()
        set(MIDL_MACHINE amd64)
    endif()

    set(MIDL_OUTPUT
            ${MIDL_OUTPUT_SOURCE}
            ${MIDL_OUTPUT_HEADER}
            ${MIDL_OUTPUT_TLB})

    # Compile the DeckLinkAPI.idl file (to generate DeckLinkAPI.h and DeckLinkAPI_i.c)
    add_custom_command(
            OUTPUT ${MIDL_OUTPUT}
            DEPENDS ${DECKLINK_API_IDL}
            COMMAND midl /env ${MIDL_MACHINE} /Oicf /header ${MIDL_OUTPUT_HEADER} ${DECKLINK_API_IDL} /iid ${MIDL_OUTPUT_SOURCE} /tlb ${MIDL_OUTPUT_TLB}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            )

    set_source_files_properties(${MIDL_OUTPUT} PROPERTIES GENERATED TRUE)

    add_custom_target(DeckLinkSDK DEPENDS ${MIDL_OUTPUT})
elseif(LINUX)
    set(DECKLINK_SDK_INCLUDE_DIR ${DECKLINK_SDK_DIR}/Linux/include)
    set(DECKLINK_SOURCES ${DECKLINK_SDK_INCLUDE_DIR}/DeckLinkAPIDispatch.cpp)
    add_library(DeckLinkSDK INTERFACE)
    target_include_directories(DeckLinkSDK INTERFACE ${DECKLINK_SDK_INCLUDE_DIR})
endif()

# DeckLink Subsystem
# ------------------

add_dependencies(${NOS_PLUGIN_TARGET} DeckLinkSDK)
target_sources(${NOS_PLUGIN_TARGET} PRIVATE ${DECKLINK_SOURCES})
target_include_directories(${NOS_PLUGIN_TARGET} PRIVATE ${DECKLINK_SDK_INCLUDE_DIR})
nos_group_targets("DeckLinkSDK" "External")

target_compile_definitions(nosDeckLinkSubsystem PRIVATE -DNOMINMAX)

# Project generation
nos_group_targets(${NOS_PLUGIN_TARGET} "NOS Subsystems")