if(HUXERUI_PROJECT_PLAN_OUTPUT)
    get_filename_component(_markdown_plan_dir "${HUXERUI_PROJECT_PLAN_OUTPUT}" DIRECTORY)
    file(MAKE_DIRECTORY "${_markdown_plan_dir}")
    file(WRITE "${HUXERUI_PROJECT_PLAN_OUTPUT}" [=[
{
  "schema": 1,
  "kind": "library",
  "name": "Lib-Markdown",
  "id": "org.huxerui.lib.markdown",
  "target": "huxerui_markdown",
  "namespace": "huxerui",
  "publicTarget": "HuxerUI::Markdown"
}
]=])
endif()
if(HUXERUI_PROJECT_PLAN_ONLY)
    return()
endif()

if(NOT HUXERUI_LIBRARY_GRAPH_ONLY)
    enable_language(C)
    enable_language(CXX)
endif()

if(HUXERUI_MARKDOWN_BUILD_VIEW)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/HuxerUIDependency.cmake")
endif()
