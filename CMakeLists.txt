cmake_minimum_required(VERSION 3.20)
if(CMAKE_HOST_APPLE AND NOT CMAKE_SYSTEM_NAME AND NOT CMAKE_TOOLCHAIN_FILE
        AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET AND "$ENV{MACOSX_DEPLOYMENT_TARGET}" STREQUAL "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment target")
endif()
project(lib_markdown VERSION 0.1.0 LANGUAGES NONE)

option(HUXERUI_MARKDOWN_BUILD_VIEW "Build the HuxerUI Markdown component" ON)
option(HUXERUI_MARKDOWN_BUILD_TESTS "Build Markdown tests" OFF)
option(HUXERUI_MARKDOWN_BUILD_EXAMPLES "Build the desktop preview" OFF)
include("${CMAKE_CURRENT_SOURCE_DIR}/HuxerUIProject.cmake")
if(HUXERUI_PROJECT_PLAN_ONLY)
    return()
endif()

set(_markdown_sources src/document.cpp third_party/md4c/md4c.c third_party/md4c/entity.c)
if(HUXERUI_MARKDOWN_BUILD_VIEW)
    huxerui_add_library(huxerui_markdown SOURCES src/markdown.cpp ${_markdown_sources})
else()
    add_library(huxerui_markdown STATIC ${_markdown_sources})
endif()
add_library(HuxerUI::Markdown ALIAS huxerui_markdown)
if(HUXERUI_LIBRARY_GRAPH_ONLY)
    return()
endif()

set_target_properties(huxerui_markdown PROPERTIES CXX_EXTENSIONS OFF C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
target_compile_features(huxerui_markdown PUBLIC cxx_std_20)
target_include_directories(huxerui_markdown PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
    PRIVATE third_party/md4c)
target_compile_options(huxerui_markdown PRIVATE
    "$<$<C_COMPILER_ID:MSVC>:/utf-8>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/W4;/permissive->"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-Wall;-Wextra;-Wpedantic>")
if(HUXERUI_MARKDOWN_BUILD_VIEW)
    target_link_libraries(huxerui_markdown PUBLIC HuxerUI::huxerui)
endif()
if(HUXERUI_MARKDOWN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(HUXERUI_MARKDOWN_BUILD_EXAMPLES)
    if(NOT HUXERUI_MARKDOWN_BUILD_VIEW)
        message(FATAL_ERROR "The preview requires HUXERUI_MARKDOWN_BUILD_VIEW=ON")
    endif()
    add_subdirectory(examples/preview)
endif()
