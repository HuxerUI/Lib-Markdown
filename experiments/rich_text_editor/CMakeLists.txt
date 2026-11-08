cmake_minimum_required(VERSION 3.20)
project(rich_text_editor_experiment VERSION 0.1.0 LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/HuxerUIDependency.cmake")

add_library(huxerui_rich_text_editor STATIC ../../src/rich_text_editor.cpp ../../src/rich_text_model.cpp)
target_compile_features(huxerui_rich_text_editor PUBLIC cxx_std_20)
set_target_properties(huxerui_rich_text_editor PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(huxerui_rich_text_editor PUBLIC ../../src)
target_link_libraries(huxerui_rich_text_editor PUBLIC HuxerUI::huxerui)
target_compile_options(huxerui_rich_text_editor PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive->"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>")

huxerui_add_app(rich_text_editor_demo SOURCES demo.cpp
    BUNDLE_NAME "Rich Text Editor" BUNDLE_IDENTIFIER "org.huxerui.rich-text-experiment")
target_link_libraries(rich_text_editor_demo PRIVATE huxerui_rich_text_editor)
target_compile_options(rich_text_editor_demo PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

enable_testing()
add_executable(rich_text_model_tests model_test.cpp)
target_include_directories(rich_text_model_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../tests")
target_link_libraries(rich_text_model_tests PRIVATE huxerui_rich_text_editor)
target_compile_options(rich_text_model_tests PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
add_test(NAME rich_text.model COMMAND rich_text_model_tests)

if(TARGET HuxerUI::testing)
    add_executable(rich_text_editor_tests editor_test.cpp)
    target_include_directories(rich_text_editor_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../tests")
    target_link_libraries(rich_text_editor_tests PRIVATE huxerui_rich_text_editor HuxerUI::testing)
    target_compile_options(rich_text_editor_tests PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    add_test(NAME rich_text.editor COMMAND rich_text_editor_tests)
endif()
