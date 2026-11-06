#pragma once

#include <huxerui/markdown/editing.h>

namespace huxerui::markdown::detail {

inline bool IsTableCell(NodeKind kind) noexcept {
  return kind == NodeKind::TableHeaderCell || kind == NodeKind::TableCell;
}

// Use MD4C's table context, including its rules for escaped pipes.
std::optional<MarkdownNode> ParseTableCell(std::string_view source, ParseOptions options);

} // namespace huxerui::markdown::detail
