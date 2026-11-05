#pragma once

#include <huxerui/markdown.h>

namespace huxerui::markdown::detail {

struct OriginalLink {
  TextRange range;
  std::string destination;
};

struct InlineResult {
  AttributedText text;
  std::vector<OriginalLink> links;
};

InlineResult BuildInline(const MarkdownNode& node, const MarkdownStyle& theme,
                         const MarkdownLinkPolicy& policy = {});

} // namespace huxerui::markdown::detail
