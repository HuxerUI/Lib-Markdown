#pragma once

#include <huxerui/markdown/editing.h>

namespace huxerui::markdown::detail {

inline bool IsTableCell(NodeKind kind) noexcept {
  return kind == NodeKind::TableHeaderCell || kind == NodeKind::TableCell;
}

// Use MD4C's table context, including its rules for escaped pipes.
std::optional<MarkdownNode> ParseTableCell(std::string_view source, ParseOptions options);
MarkdownRichText RichProjection(const MarkdownNode& node, bool nested, ParseOptions options);

struct MarkdownBlockPosition {
  std::size_t block = 0;
  TextOffset offset = 0;
  bool operator==(const MarkdownBlockPosition&) const = default;
};

// Source-backed replacement across root paragraphs and headings. Positions
// refer to rendered UTF-16 text in logical blocks, omitting BlankLines nodes.
// A newly emptied paragraph retains an anchor in the whitespace of the draft.
class MarkdownDocumentEditSession {
public:
  explicit MarkdownDocumentEditSession(std::string_view source, ParseOptions options = {});
  MarkdownDocumentEditSession(const MarkdownDocumentEditSession& other);
  [[nodiscard]] std::string_view Source() const noexcept { return parser_.Source(); }
  [[nodiscard]] const MarkdownDocument& Document() const noexcept { return parser_.Document(); }
  [[nodiscard]] std::size_t BlockCount() const noexcept { return blocks_.size(); }
  [[nodiscard]] const MarkdownNode& Block(std::size_t index) const;
  [[nodiscard]] std::optional<std::size_t> SyntaxIndex(std::size_t index) const;
  [[nodiscard]] std::optional<MarkdownSourceRange> SourceRange(std::size_t index) const;
  MarkdownBlockPosition Replace(MarkdownBlockPosition first, MarkdownBlockPosition last,
                                              std::string_view replacement);
  bool ToggleMark(MarkdownBlockPosition first, MarkdownBlockPosition last, NodeKind mark);
  void ReplaceNested(std::size_t block, std::span<const std::size_t> path, TextRange range,
                     std::string_view replacement);
  bool ToggleNestedMark(std::size_t block, std::span<const std::size_t> path, TextRange range, NodeKind mark);

private:
  struct BlockLocation {
    std::optional<std::size_t> syntax_index;
    std::optional<MarkdownSourceRange> source;
  };
  struct FormattedBlock {
    MarkdownSourceRange range;
    std::optional<MarkdownNode> node;
    bool verify_range = true;
  };
  [[nodiscard]] FormattedBlock AppendFormatted(std::string& source, std::size_t base,
                                              const MarkdownRichText& text, NodeKind kind) const;
  void ApplySplice(std::size_t first, std::size_t last, const MarkdownEdit& edit,
                   std::span<const FormattedBlock> formatted);
  [[nodiscard]] const MarkdownNode& NestedBlock(std::size_t block, std::span<const std::size_t> path) const;
  void ApplyNested(std::size_t block, std::span<const std::size_t> path, std::string replacement);
  ParseOptions options_;
  MarkdownParseSession parser_;
  std::vector<BlockLocation> blocks_;
};

} // namespace huxerui::markdown::detail
