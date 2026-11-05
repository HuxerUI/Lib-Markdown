if(HUXERUI_PROJECT_PLAN_OUTPUT)
    get_filename_component(_preview_plan_dir "${HUXERUI_PROJECT_PLAN_OUTPUT}" DIRECTORY)
    file(MAKE_DIRECTORY "${_preview_plan_dir}")
    file(WRITE "${HUXERUI_PROJECT_PLAN_OUTPUT}" [=[
{
  "schema": 1,
  "kind": "app",
  "name": "Markdown Preview",
  "id": "org.huxerui.markdown.preview",
  "target": "markdown_preview"
}
]=])
endif()
if(HUXERUI_PROJECT_PLAN_ONLY)
    return()
endif()
if(NOT HUXERUI_LIBRARY_GRAPH_ONLY)
    enable_language(CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/HuxerUIDependency.cmake")
