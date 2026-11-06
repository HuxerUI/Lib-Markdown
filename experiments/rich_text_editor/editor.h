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

struct InlineFormatActions {
  std::function<std::optional<TextSelection>()> selection;
  std::function<void(InlineFormat, TextSelection)> toggle;
};

using RenderParagraph = std::function<View(std::size_t, AttributedText, TextStyle, TextAlign)>;

struct EditorLayout {
  std::vector<TextBlockId> item_ids;
  std::function<std::size_t(std::size_t)> item_for_paragraph;
  std::function<View(std::size_t, const RenderParagraph&)> render_item;
};

struct EditorOptions {
  std::optional<TextStyle> base_style;
  float paragraph_spacing = 0.0F;
  std::function<EditorLayout(const Document&)> layout;
  std::function<void(const Document&, std::optional<TextRange>)> changed;
  std::function<void(std::string_view)> rejected;
  std::shared_ptr<InlineFormatActions> format_actions;
};

[[nodiscard]] View RichTextEditor(Document document, EditorOptions options);

} // namespace detail

} // namespace huxerui::rich_text
