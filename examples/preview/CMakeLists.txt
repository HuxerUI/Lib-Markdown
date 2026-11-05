huxerui_add_app(markdown_preview
    SOURCES main.cpp
    BUNDLE_NAME "HuxerUI Markdown"
    BUNDLE_IDENTIFIER "org.huxerui.markdown.preview")
huxerui_use_library(markdown_preview TARGET HuxerUI::Markdown)
target_compile_options(markdown_preview PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
