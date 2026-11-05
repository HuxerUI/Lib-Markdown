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

InlineResult BuildInline(const MarkdownNode& node, const MarkdownTheme& theme,
                         const std::function<bool(std::string_view)>& policy = {});

} // namespace huxerui::markdown::detail
