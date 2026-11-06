#pragma once

#include <huxerui/markdown.h>

#include "editor.h"

namespace huxerui::markdown::detail {

// The document holds an attributed projection and its authoritative Markdown snapshot.
[[nodiscard]] rich_text::detail::Document EditableMarkdownDocument(
    std::string_view source, ParseOptions options);
[[nodiscard]] std::string_view EditableMarkdownSource(const rich_text::detail::Document& document);
[[nodiscard]] const MarkdownDocument& EditableMarkdownSyntax(const rich_text::detail::Document& document);
[[nodiscard]] rich_text::detail::EditorLayout EditableMarkdownLayout(const rich_text::detail::Document& document,
                                                                      MarkdownStyle style);

} // namespace huxerui::markdown::detail
