#pragma once
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

/// Thread-confined block editing model. Source remains authoritative and is never
/// reconstructed from the AST. Only verified root paragraphs/headings can begin
/// an edit; their replacement may create any Markdown structure, including several
/// blocks. LayoutDocument/LayoutSource stay frozen until EndEdit so the active
/// native TextField can retain its identity, selection and IME session.
class MarkdownBlockEditSession final {
public:
  explicit MarkdownBlockEditSession(std::string_view source = {}, ParseOptions options = {});
  [[nodiscard]] std::string_view Source() const noexcept;
  [[nodiscard]] const MarkdownDocument& Document() const noexcept;
  [[nodiscard]] const MarkdownDocument& LayoutDocument() const noexcept;
  [[nodiscard]] std::string_view LayoutSource() const noexcept;
  [[nodiscard]] std::optional<std::size_t> EditingBlock() const noexcept;
  [[nodiscard]] const TextEditingValue& EditingValue() const noexcept;
  /// Update the whole source, ending any block edit. Equal source is a no-op.
  /// Composition must finish before changing source or ending/switching blocks.
  /// All parsing/input failures preserve the previous source and editing value.
  void UpdateSource(std::string_view source);
  void BeginEdit(std::size_t block);
  void UpdateEdit(const TextEditingValue& value);
  void ApplyCommand(MarkdownEditCommand command);
  void EndEdit();
private:
  ParseOptions options_;
  MarkdownParseSession parser_;
  MarkdownDocument layout_document_;
  std::string layout_source_;
  std::optional<std::size_t> block_;
  MarkdownSourceRange range_;
  TextEditingValue value_;
};

} // namespace huxerui::markdown
