#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace huxerui::markdown {

enum class NodeKind {
  Document, Paragraph, Heading, Quote, UnorderedList, OrderedList, ListItem,
  Rule, CodeBlock, Table, TableHead, TableBody, TableRow, TableHeaderCell,
  TableCell, Emphasis, Strong, Strikethrough, Code, Link, Image, Text,
};

enum class CellAlignment { Default, Left, Center, Right };

/// Owned UTF-8 syntax tree. No node borrows the input or MD4C callback storage.
struct MarkdownNode {
  NodeKind kind = NodeKind::Text;
  std::string text;
  std::vector<MarkdownNode> children;
  unsigned heading_level = 0;
  unsigned list_start = 1;
  bool tight = false;
  std::optional<bool> task_checked;
  std::string destination;
  std::string title;
  std::string info;
  std::string language;
  CellAlignment alignment = CellAlignment::Default;

  bool operator==(const MarkdownNode&) const = default;
};

struct ParseOptions {
  bool tables = true;
  bool strikethrough = true;
  bool task_lists = true;
  bool autolinks = true;
  bool hard_soft_breaks = false;
  std::size_t max_input_bytes = 8 * 1024 * 1024;
  std::size_t max_nesting = 128;
};

/// Immutable shared document; safe to prepare on a worker and reuse during composition.
class MarkdownDocument final {
public:
  [[nodiscard]] const MarkdownNode& Root() const noexcept { return root_; }
  [[nodiscard]] std::string PlainText() const;

private:
  explicit MarkdownDocument(MarkdownNode root);
  MarkdownNode root_;
  friend std::shared_ptr<const MarkdownDocument> ParseMarkdown(std::string_view, ParseOptions);
};

/// Parse CommonMark plus configurable GFM tables, tasks, autolinks and strikethrough.
/// HTML is literal text. Invalid UTF-8/options throw invalid_argument; resource limits
/// throw length_error. Parsing errors throw runtime_error; allocation errors propagate.
[[nodiscard]] std::shared_ptr<const MarkdownDocument>
ParseMarkdown(std::string_view source, ParseOptions options = {});

/// Default interaction policy: http(s), mailto and relative targets are allowed.
/// Other explicit URI schemes, protocol-relative URLs and ASCII controls are rejected.
/// Parsing always preserves destinations; the renderer applies this policy to links.
[[nodiscard]] bool IsSafeMarkdownLink(std::string_view target) noexcept;

/// Visible content of a node; inline markup is removed, images contribute alt text.
[[nodiscard]] std::string PlainText(const MarkdownNode& node);

} // namespace huxerui::markdown
