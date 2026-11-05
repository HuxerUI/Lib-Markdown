#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace huxerui::markdown {

/// CommonMark/GFM syntax kinds; Code is inline code, CodeBlock is block content.
enum class NodeKind {
  Document, Paragraph, Heading, BlockQuote, UnorderedList, OrderedList, ListItem,
  ThematicBreak, CodeBlock, Table, TableHead, TableBody, TableRow, TableHeaderCell,
  TableCell, Emphasis, Strong, Strikethrough, Code, Link, Image, Text,
};

/// Source alignment for a table cell. Default uses leading paragraph alignment.
enum class CellAlignment { Default, Left, Center, Right };

/// Owned UTF-8 syntax tree. No node borrows the input or MD4C callback storage.
struct MarkdownNode {
  NodeKind kind = NodeKind::Text;
  /// UTF-8 literal for Text, Code and CodeBlock; other nodes store text in children.
  std::string text;
  std::vector<MarkdownNode> children;
  /// 1..6 for Heading; zero otherwise.
  unsigned heading_level = 0;
  /// First ordinal of OrderedList; default 1.
  unsigned list_start = 1;
  /// Whether list items omit blank paragraph separation.
  bool tight = false;
  /// Only task ListItem nodes have a value; false means unchecked.
  std::optional<bool> task_checked;
  /// Entity-decoded link/image destination; not resolved against a base or fetched.
  std::string destination;
  std::string title;
  /// Complete decoded code-fence info string; empty for indented code or other nodes.
  std::string fence_info;
  /// First word of fence_info; empty when no language was supplied.
  std::string language;
  CellAlignment alignment = CellAlignment::Default;

  bool operator==(const MarkdownNode&) const = default;
};

/// Controls display of an ordinary source line break; explicit hard breaks are unchanged.
enum class SoftBreakMode {
  /// Merge an ordinary source newline into one space (default).
  Space,
  /// Preserve an ordinary source newline as a displayed line break.
  LineBreak,
};

/// Synchronous parse configuration. No UI configuration or thread scheduling.
struct ParseOptions {
  bool tables = true;
  bool strikethrough = true;
  bool task_lists = true;
  bool autolinks = true;
  /// Does not change explicit hard breaks or newlines inside code blocks/spans.
  SoftBreakMode soft_break_mode = SoftBreakMode::Space;
  /// Positive UTF-8 input-byte limit, default 8 MiB; exceeding it throws length_error.
  std::size_t max_input_bytes = 8 * 1024 * 1024;
  /// Positive nesting limit, default 128, maximum 512; exceeded depth throws length_error.
  std::size_t max_nesting = 128;
};

/// Immutable, cheaply copied document snapshot. Copies share storage; equality compares
/// snapshot identity, not AST content. Safe to prepare on a worker and reuse on the UI thread.
class MarkdownDocument final {
public:
  /// Creates a valid empty document with a Document root.
  MarkdownDocument();
  MarkdownDocument(const MarkdownDocument&) = default;
  MarkdownDocument& operator=(const MarkdownDocument&) = default;
  /// Moving retains a valid source sharing the same snapshot; no tree is copied.
  MarkdownDocument(MarkdownDocument&& other) noexcept : storage_(other.storage_) {}
  MarkdownDocument& operator=(MarkdownDocument&& other) noexcept {
    storage_ = other.storage_;
    return *this;
  }

  /// Borrows the root while an owner of this snapshot remains alive.
  [[nodiscard]] const MarkdownNode& Root() const noexcept;
  /// Returns owned UTF-8 text without markup.
  [[nodiscard]] std::string PlainText() const;
  /// Snapshot identity equality; separately parsed equivalent ASTs need not be equal.
  bool operator==(const MarkdownDocument&) const noexcept = default;

private:
  struct Storage;
  explicit MarkdownDocument(MarkdownNode root);
  std::shared_ptr<const Storage> storage_;
  friend MarkdownDocument ParseMarkdown(std::string_view, ParseOptions);
};

/// Parse CommonMark plus configurable GFM tables, tasks, autolinks and strikethrough.
/// HTML is literal text. Invalid UTF-8/options throw invalid_argument; resource limits
/// throw length_error. Parsing errors throw runtime_error; allocation errors propagate.
/// May run on a worker. Owns all resulting text; the source need only survive this call.
/// Empty source yields a valid empty document; no incremental state is retained.
[[nodiscard]] MarkdownDocument ParseMarkdown(std::string_view source, ParseOptions options = {});

/// Default interaction policy: http(s), mailto and relative targets are allowed.
/// Other explicit URI schemes, protocol-relative URLs and ASCII controls are rejected.
/// Parsing always preserves destinations; the renderer applies this policy to links.
/// This is an allow-policy predicate, not URI validation or a navigation safety guarantee.
[[nodiscard]] bool IsDefaultLinkAllowed(std::string_view target) noexcept;

/// Visible content of a node; inline markup is removed, images contribute alt text.
[[nodiscard]] std::string PlainText(const MarkdownNode& node);

} // namespace huxerui::markdown
