if(HUXERUI_PROJECT_PLAN_OUTPUT)
    get_filename_component(_editor_plan_dir "${HUXERUI_PROJECT_PLAN_OUTPUT}" DIRECTORY)
    file(MAKE_DIRECTORY "${_editor_plan_dir}")
    file(WRITE "${HUXERUI_PROJECT_PLAN_OUTPUT}" [=[
{"schema":1,"kind":"app","name":"Markdown Writer","id":"org.huxerui.markdown.editor","target":"markdown_editor"}
]=])
endif()
if(HUXERUI_PROJECT_PLAN_ONLY)
    return()
endif()
if(NOT HUXERUI_LIBRARY_GRAPH_ONLY)
    enable_language(CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/HuxerUIDependency.cmake")
