module;

#include <huxerui/markdown.h>

namespace huxerui_markdown_macros_capture {
inline constexpr auto value_HUXERUI_MARKDOWN_SOURCE_MAPPING = HUXERUI_MARKDOWN_SOURCE_MAPPING;
} // namespace huxerui_markdown_macros_capture

#undef HUXERUI_MARKDOWN_SOURCE_MAPPING

export module huxerui.markdown.macros;

export namespace huxerui::markdown::macros {
inline constexpr auto HUXERUI_MARKDOWN_SOURCE_MAPPING =
    huxerui_markdown_macros_capture::value_HUXERUI_MARKDOWN_SOURCE_MAPPING;
} // namespace huxerui::markdown::macros
