#pragma once

#include <functional>
#include <vector>

#include <huxerui/text.h>
#include <huxerui/view.h>

#include "model.h"

namespace huxerui::rich_text {

/// Creates an experimental document editor initialized with independent attributed paragraphs.
[[nodiscard]] View RichTextEditor(std::vector<AttributedText> paragraphs);

namespace detail {

struct EditorOptions {
  std::optional<TextStyle> base_style;
  float paragraph_spacing = 0.0F;
  std::function<AttributedText(const Document&, std::size_t)> display;
  std::function<void(const Document&, std::optional<TextRange>)> changed;
  std::function<void(std::string_view)> rejected;
};

[[nodiscard]] View RichTextEditor(Document document, EditorOptions options);

} // namespace detail

} // namespace huxerui::rich_text
