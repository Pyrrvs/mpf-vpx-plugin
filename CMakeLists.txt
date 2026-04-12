cmake_minimum_required(VERSION 3.20)
project(mpf-vpx-plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# VPX SDK headers via FetchContent
set(VPX_TAG "v10.8.1-3788-2151290" CACHE STRING "VPX git tag to fetch SDK headers from")

include(FetchContent)
FetchContent_Declare(vpx-sdk
    GIT_REPOSITORY https://github.com/vpinball/vpinball.git
    GIT_TAG ${VPX_TAG}
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vpx-sdk)

# Detect VPX SDK header location (moved between versions)
if(EXISTS "${vpx-sdk_SOURCE_DIR}/plugins/plugins/MsgPlugin.h")
    set(VPX_PLUGIN_INCLUDE_DIR "${vpx-sdk_SOURCE_DIR}/plugins")
elseif(EXISTS "${vpx-sdk_SOURCE_DIR}/src/plugins/MsgPlugin.h")
    set(VPX_PLUGIN_INCLUDE_DIR "${vpx-sdk_SOURCE_DIR}/src")
else()
    message(FATAL_ERROR "Could not find VPX plugin SDK headers in fetched VPX source")
endif()
message(STATUS "VPX plugin SDK headers found at: ${VPX_PLUGIN_INCLUDE_DIR}")

# --- Plugin shared library ---
add_library(mpf-vpx-plugin MODULE
    src/MPFPlugin.cpp
)

target_include_directories(mpf-vpx-plugin PRIVATE
    ${VPX_PLUGIN_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

# Platform-specific output naming and linking
if(WIN32)
    target_link_libraries(mpf-vpx-plugin PRIVATE ws2_32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set_target_properties(mpf-vpx-plugin PROPERTIES
            PREFIX ""
            OUTPUT_NAME "plugin-mpf64"
        )
    else()
        set_target_properties(mpf-vpx-plugin PROPERTIES
            PREFIX ""
            OUTPUT_NAME "plugin-mpf"
        )
    endif()
elseif(APPLE)
    set_target_properties(mpf-vpx-plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "plugin-mpf"
        SUFFIX ".dylib"
    )
else()
    set_target_properties(mpf-vpx-plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "plugin-mpf"
    )
endif()

# Install target: copies plugin.cfg + binary into dist/mpf/
set(DIST_DIR "${CMAKE_BINARY_DIR}/dist/mpf")
add_custom_command(TARGET mpf-vpx-plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/plugin.cfg" "${DIST_DIR}/plugin.cfg"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:mpf-vpx-plugin>" "${DIST_DIR}"
)

# --- Test executable ---
enable_testing()

add_executable(mpf-vpx-tests
    tests/test_main.cpp
)

target_include_directories(mpf-vpx-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/src
    ${VPX_PLUGIN_INCLUDE_DIR}
)

if(WIN32)
    target_link_libraries(mpf-vpx-tests PRIVATE ws2_32)
endif()

add_test(NAME mpf-vpx-tests COMMAND mpf-vpx-tests)
