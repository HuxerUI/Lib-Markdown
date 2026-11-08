#pragma once

#include <huxerui/markdown.h>

#include "rich_text_editor.h"

namespace huxerui::markdown::detail {

// The document holds an attributed projection and its authoritative Markdown snapshot.
[[nodiscard]] rich_text::detail::Document EditableMarkdownDocument(
    std::string_view source, ParseOptions options);
[[nodiscard]] std::string_view EditableMarkdownSource(const rich_text::detail::Document& document);
[[nodiscard]] const MarkdownDocument& EditableMarkdownSyntax(const rich_text::detail::Document& document);
[[nodiscard]] TextEditingValue EditableMarkdownValue(const rich_text::detail::Document& document,
    TextSelection selection, std::optional<TextRange> composition = {});
[[nodiscard]] rich_text::detail::EditorLayout EditableMarkdownLayout(const rich_text::detail::Document& document,
    MarkdownOptions options, MarkdownStyle style, EventEmitter events = {});

/// Shared document-mode view. The caller retains the edit session when history must survive unmounting.
[[nodiscard, huxerui::composable]] View MarkdownDocumentEditor(std::shared_ptr<rich_text::detail::EditSession> edit,
    MarkdownEditorOptions options, std::function<void(const TextEditingValue&)> changed,
    std::function<void(std::string_view)> rejected,
    std::shared_ptr<rich_text::detail::InlineFormatActions> format_actions = {});

/// Existing per-block path, also used for source-only editing and unsupported document projections.
[[nodiscard]] View MarkdownBlockEditor(TextEditingValue value, MarkdownEditorOptions options);

} // namespace huxerui::markdown::detail
