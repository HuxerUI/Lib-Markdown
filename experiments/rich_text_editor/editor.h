#pragma once

#include <vector>

#include <huxerui/text.h>
#include <huxerui/view.h>

namespace huxerui::rich_text {

/// Creates an experimental document editor initialized with independent attributed paragraphs.
[[nodiscard]] View RichTextEditor(std::vector<AttributedText> paragraphs);

} // namespace huxerui::rich_text
