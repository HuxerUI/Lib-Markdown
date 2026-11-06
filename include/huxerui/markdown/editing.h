#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <huxerui/text_input.h>
#include <huxerui/markdown/document.h>

namespace huxerui::markdown {

enum class MarkdownEditCommand { Bold, Italic, InlineCode, Heading1, Heading2, BulletList, BlockQuote };

/// Pure source-editing operation on HuxerUI UTF-16 selections. Returns an owned
/// value, preserving selection direction/affinity; never mutates the input.
/// Inline commands wrap the selection (or place a caret between empty markers).
/// Block commands prefix selected lines, excluding an end exactly at the next
/// line's start. Headings replace an existing ATX prefix; list/quote prefixes are
/// idempotent. CRLF is retained. This is not an AST transaction or format toggle.
/// Throws invalid_argument for invalid UTF-8, selections, commands or multiline
/// inline code; logic_error during active IME composition (finish it first).
/// No history, parsing, UI state or implicit focus changes are performed.
[[nodiscard]] TextEditingValue ApplyMarkdownCommand(const TextEditingValue& value, MarkdownEditCommand command);

/// Semantic inline marks retained while editing displayed text. Source spelling
/// within an edited block may be normalized; neighbouring source is untouched.
struct MarkdownTextMark {
  NodeKind kind = NodeKind::Strong;
  std::string destination;
  std::string title;
  bool operator==(const MarkdownTextMark&) const = default;
};
struct MarkdownRichTextRun {
  TextRange range;
  std::vector<MarkdownTextMark> marks;
};

/// Display-text editing model for paragraphs, headings and table cells with supported inline marks.
/// Input coordinates are visible UTF-16 positions, excluding Markdown delimiters.
/// Replacements inherit the first replaced character's marks, or the preceding
/// character's marks at an insertion boundary. Composition cancellation restores
/// its original formatting. Serialization is verified with MD4C before acceptance.
class MarkdownRichText final {
public:
  [[nodiscard]] static bool Supports(const MarkdownNode& node) noexcept;
  explicit MarkdownRichText(const MarkdownNode& node, ParseOptions options = {});
  [[nodiscard]] const TextEditingValue& Value() const noexcept { return value_; }
  [[nodiscard]] std::vector<MarkdownRichTextRun> Runs() const;
  [[nodiscard]] TextEditingValue SourceValue() const;
  void Update(const TextEditingValue& value);
private:
  struct Character {
    std::string text;
    std::vector<MarkdownTextMark> marks;
    bool operator==(const Character&) const = default;
  };
  TextEditingValue value_;
  std::vector<Character> characters_;
  std::optional<std::vector<Character>> composition_baseline_;
  std::string composition_text_;
  struct Revision { std::string text; std::string source; };
  std::vector<Revision> history_;
  std::size_t history_bytes_ = 0;
  NodeKind kind_;
  unsigned heading_level_ = 0;
  ParseOptions options_;
  void Append(const MarkdownNode& node, std::vector<MarkdownTextMark> marks);
  bool SetMark(TextRange range, NodeKind kind, bool enabled);
  [[nodiscard]] TextEditingValue Serialize(bool entities) const;
  friend class detail::MarkdownDocumentEditSession;
};

/// Thread-confined block editing model. Source remains authoritative and is never
/// reconstructed from the AST. A root replacement can create any Markdown
/// structure. Nested edits address single-line content and preserve the rest of
/// the tree. LayoutDocument/LayoutSource stay frozen until EndEdit so the active
/// native TextField can retain its identity, selection and IME session.
class MarkdownBlockEditSession final {
public:
  explicit MarkdownBlockEditSession(std::string_view source = {}, ParseOptions options = {});
  [[nodiscard]] std::string_view Source() const noexcept;
  [[nodiscard]] const MarkdownDocument& Document() const noexcept;
  [[nodiscard]] const MarkdownDocument& LayoutDocument() const noexcept;
  [[nodiscard]] std::string_view LayoutSource() const noexcept;
  [[nodiscard]] std::optional<std::size_t> EditingBlock() const noexcept;
  [[nodiscard]] std::span<const std::size_t> EditingPath() const noexcept { return path_; }
  [[nodiscard]] const TextEditingValue& EditingValue() const noexcept;
  /// Update the whole source, ending any block edit. Equal source is a no-op.
  /// Composition must finish before changing source or ending/switching blocks.
  /// All parsing/input failures preserve the previous source and editing value.
  void UpdateSource(std::string_view source);
  void BeginEdit(std::size_t block);
  void BeginEdit(std::span<const std::size_t> path);
  void UpdateEdit(const TextEditingValue& value);
  void ApplyCommand(MarkdownEditCommand command);
  void EndEdit();
private:
  ParseOptions options_;
  MarkdownParseSession parser_;
  MarkdownDocument layout_document_;
  std::string layout_source_;
  std::vector<std::size_t> path_;
  MarkdownSourceRange range_;
  TextEditingValue value_;
};

} // namespace huxerui::markdown
