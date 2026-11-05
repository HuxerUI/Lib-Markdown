#pragma once
#include <huxerui/text_input.h>

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

} // namespace huxerui::markdown
