cmake_minimum_required(VERSION 3.20)

project(AheadLibEx_version LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR
        "This project builds a Windows version.dll proxy and only supports Windows."
    )
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "This source set is x64-only. Please select an x64 generator/toolchain."
    )
endif()

if(MSVC)
    enable_language(ASM_MASM)
    set(AHEADLIBEX_ASM "version_x64_jump.asm")
else()
    enable_language(ASM)
    set(AHEADLIBEX_ASM "version_x64_jump.S")
endif()

set(THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty")


add_library(${PROJECT_NAME} SHARED
    version_x64.c
    simple_controller.cpp
    simple_controller.h
    SpiderSolitaireHack.cpp
    SpiderSolitaireHack.h
    "${THIRDPARTY_DIR}/safetyhook.cpp"
    "${THIRDPARTY_DIR}/Zydis.c"
    ${AHEADLIBEX_ASM}
    version.def
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "version"
    PREFIX ""
    DEBUG_POSTFIX ""

    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF

    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF

    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${THIRDPARTY_DIR}"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    user32
    psapi
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Ob2>
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Release>:/Gw>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Release>:/Zc:inline>
        /wd4005
    )

    target_link_options(${PROJECT_NAME} PRIVATE
        "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/version.def"

        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/INCREMENTAL:NO>
    )
else()
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/version.def"
    )
endif()