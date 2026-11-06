#include "document_editor_internal.h"
#include "editing_internal.h"
#include "inline.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace huxerui::markdown::detail {
namespace {
namespace editor = rich_text::detail;

class MarkdownBacking final : public editor::DocumentBacking {
public:
  MarkdownBacking(std::string_view source, ParseOptions options) : session_(source, options), options_(options) {}

  std::string_view Source() const noexcept { return session_.Source(); }
  const MarkdownDocument& Syntax() const noexcept { return session_.Document(); }

  std::vector<editor::Paragraph> Paragraphs() const {
    std::vector<editor::Paragraph> paragraphs;
    paragraphs.reserve(session_.BlockCount());
    for (std::size_t index = 0; index < session_.BlockCount(); ++index)
      paragraphs.push_back({static_cast<TextBlockId>(index) + 1, ParagraphText(index)});
    if (paragraphs.empty()) throw std::invalid_argument("Markdown document projection requires a text block");
    return paragraphs;
  }

  AttributedText Display(std::size_t index, const MarkdownStyle& style, const MarkdownLinkPolicy& policy) const {
    const auto& node = session_.Block(index);
    auto base = style.body;
    if (node.kind == NodeKind::Heading) {
      const auto level = std::clamp(node.heading_level, 1U, 6U);
      base.font = base.font.WithSize(style.heading_sizes[level - 1]).WithWeight(FontWeight::Bold);
    }
    const MarkdownRichText text(node, options_);
    return AttributedText::FromRanges(text.Value().text, BuildEditableStyles(text, base, style, policy));
  }

  editor::Document Replace(const editor::Document& document, TextRange target,
                           std::string_view replacement, TextBlockId& next_id) const override {
    const auto first = document.Locate(target.start);
    const auto last = document.Locate(target.end);
    auto next = std::make_shared<MarkdownBacking>(*this);
    const auto caret = next->session_.Replace({first.index, first.offset}, {last.index, last.offset}, replacement);
    const auto previous = document.Paragraphs();
    const auto prefix = previous.first(first.index);
    const auto suffix = previous.subspan(last.index + 1);
    std::vector<editor::Paragraph> paragraphs(prefix.begin(), prefix.end());
    paragraphs.push_back({previous[first.index].id, next->ParagraphText(first.index)});
    for (auto index = first.index + 1; index <= caret.block; ++index) {
      if (next_id == std::numeric_limits<TextBlockId>::max())
        throw std::length_error("Rich text paragraph identifiers are exhausted");
      paragraphs.push_back({next_id++, next->ParagraphText(index)});
    }
    paragraphs.insert(paragraphs.end(), suffix.begin(), suffix.end());
    return editor::Document(std::move(paragraphs), std::move(next));
  }

  editor::Document ToggleFormat(const editor::Document& document, TextRange target,
                                editor::InlineFormat format) const override {
    NodeKind mark;
    switch (format) {
    case editor::InlineFormat::Bold: mark = NodeKind::Strong; break;
    case editor::InlineFormat::Italic: mark = NodeKind::Emphasis; break;
    case editor::InlineFormat::Code: mark = NodeKind::Code; break;
    default: throw std::invalid_argument("Markdown inline format is invalid");
    }
    const auto first = document.Locate(target.start);
    const auto last = document.Locate(target.end);
    auto next = std::make_shared<MarkdownBacking>(*this);
    if (!next->session_.ToggleMark({first.index, first.offset}, {last.index, last.offset}, mark)) return document;
    const auto paragraphs = document.Paragraphs();
    return editor::Document({paragraphs.begin(), paragraphs.end()}, std::move(next));
  }

private:
  AttributedText ParagraphText(std::size_t index) const {
    const auto& node = session_.Block(index);
    if (!MarkdownRichText::Supports(node) || !session_.SourceRange(index))
      throw std::invalid_argument("Markdown document projection requires editable paragraphs and headings");
    const MarkdownRichText rich(node, options_);
    if (rich.Value().text.find_first_of("\r\n") != std::string::npos)
      throw std::invalid_argument("Markdown paragraphs with embedded line breaks require block or source editing");
    return AttributedText(rich.Value().text);
  }

  MarkdownDocumentEditSession session_;
  ParseOptions options_;
};

const MarkdownBacking& Backing(const editor::Document& document) {
  const auto* backing = dynamic_cast<const MarkdownBacking*>(document.Backing().get());
  if (!backing) throw std::invalid_argument("Document has no Markdown source");
  return *backing;
}

} // namespace

rich_text::detail::Document EditableMarkdownDocument(std::string_view source, ParseOptions options) {
  auto backing = std::make_shared<MarkdownBacking>(source, options);
  auto paragraphs = backing->Paragraphs();
  return rich_text::detail::Document(std::move(paragraphs), std::move(backing));
}

std::string_view EditableMarkdownSource(const rich_text::detail::Document& document) {
  return Backing(document).Source();
}

const MarkdownDocument& EditableMarkdownSyntax(const rich_text::detail::Document& document) {
  return Backing(document).Syntax();
}

AttributedText EditableMarkdownParagraph(const rich_text::detail::Document& document, std::size_t index,
                                         const MarkdownStyle& style, const MarkdownLinkPolicy& policy) {
  return Backing(document).Display(index, style, policy);
}

} // namespace huxerui::markdown::detail
