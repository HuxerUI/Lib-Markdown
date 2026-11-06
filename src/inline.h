#pragma once

#include <huxerui/markdown.h>
#include <set>

namespace huxerui::markdown::detail {

struct OriginalLink {
  TextRange range;
  std::string destination;
  NodeKind kind = NodeKind::Link;
  unsigned footnote_number = 0;
  std::string footnote_label;
  unsigned footnote_reference = 0;
  unsigned spoiler_index = 0;
};

struct InlineResult {
  AttributedText text;
  std::vector<OriginalLink> links;
};

InlineResult BuildInline(const MarkdownNode& node, const MarkdownStyle& theme,
                         const MarkdownLinkPolicy& policy = {}, const std::set<unsigned>* revealed_spoilers = nullptr);
std::vector<TextStyleRange> BuildEditableStyles(const MarkdownRichText& rich, const TextStyle& base,
                                                const MarkdownStyle& theme, const MarkdownLinkPolicy& policy);
std::vector<std::vector<std::size_t>> DocumentEditablePaths(const MarkdownDocument& document, std::string_view source,
                                                          std::size_t block);
View RenderEditableBlock(const MarkdownNode& node, const MarkdownStyle& style,
                         std::function<std::optional<View>(const MarkdownNode&, TextStyle, TextAlign)> editor);

} // namespace huxerui::markdown::detail
