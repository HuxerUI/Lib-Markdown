#include "document_editor_internal.h"
#include "editing_internal.h"
#include "inline.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace huxerui::markdown::detail {
namespace {
namespace editor = rich_text::detail;

class MarkdownBacking final : public editor::DocumentBacking {
public:
  MarkdownBacking(std::string_view source, ParseOptions options) : session_(source, options), options_(options) {
    MapProjection();
  }

  std::string_view Source() const noexcept { return session_.Source(); }
  const MarkdownDocument& Syntax() const noexcept { return session_.Document(); }

  std::vector<editor::Paragraph> Paragraphs() const {
    std::vector<editor::Paragraph> paragraphs;
    paragraphs.reserve(leaves_.size());
    for (std::size_t index = 0; index < leaves_.size(); ++index)
      paragraphs.push_back({static_cast<TextBlockId>(index) + 1, ParagraphText(index)});
    if (paragraphs.empty()) throw std::invalid_argument("Markdown document projection requires a text block");
    return paragraphs;
  }

  std::size_t ItemForParagraph(std::size_t index) const { return leaves_.at(index).block; }

  std::vector<TextBlockId> ItemIds(std::span<const editor::Paragraph> paragraphs) const {
    std::vector<TextBlockId> ids;
    ids.reserve(session_.BlockCount());
    auto protected_id = std::numeric_limits<TextBlockId>::max();
    for (std::size_t block = 0; block < session_.BlockCount(); ++block) {
      const auto first = block_starts_[block];
      // Protected groups keep their relative order across accepted edits.
      ids.push_back(first < block_starts_[block + 1] ? paragraphs[first].id : protected_id--);
    }
    return ids;
  }

  View RenderItem(std::size_t block, const MarkdownStyle& style, const editor::RenderParagraph& render) const {
    return RenderEditableBlock(session_.Block(block), style,
        [&](const MarkdownNode& node, TextStyle base, TextAlign align) -> std::optional<View> {
          auto index = block_starts_[block];
          if (session_.SyntaxIndex(block)) {
            const auto found = indexes_.find(&node);
            if (found == indexes_.end()) return std::nullopt;
            index = found->second;
          }
          const MarkdownRichText rich(node, options_);
          auto displayed = AttributedText::FromRanges(rich.Value().text, BuildEditableStyles(rich, base, style, {}));
          return render(index, std::move(displayed), std::move(base), align);
        });
  }

  editor::Document Replace(const editor::Document& document, TextRange target,
                           std::string_view replacement, TextBlockId& next_id) const override {
    const auto first = document.Locate(target.start);
    const auto last = document.Locate(target.end);
    const auto& head = leaves_[first.index];
    const auto& tail = leaves_[last.index];
    const bool nested = IsNestedRange(first.index, last.index);
    auto next = std::make_shared<MarkdownBacking>(*this);
    auto count = std::size_t{1};
    if (nested) {
      next->session_.ReplaceNested(head.block, head.path, {first.offset, last.offset}, replacement);
    } else {
      const auto caret = next->session_.Replace({head.block, first.offset}, {tail.block, last.offset}, replacement);
      count = caret.block - head.block + 1;
    }
    next->MapProjection();
    const auto previous = document.Paragraphs();
    const auto prefix = previous.first(first.index);
    const auto suffix = previous.subspan(last.index + 1);
    if (next->leaves_.size() != prefix.size() + count + suffix.size())
      throw std::invalid_argument("Use block or source editing to change this Markdown structure");
    std::vector<editor::Paragraph> paragraphs(prefix.begin(), prefix.end());
    paragraphs.push_back({previous[first.index].id, next->ParagraphText(first.index)});
    for (auto index = first.index + 1; index < first.index + count; ++index) {
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
    const auto& head = leaves_[first.index];
    const auto& tail = leaves_[last.index];
    const bool nested = IsNestedRange(first.index, last.index);
    auto next = std::make_shared<MarkdownBacking>(*this);
    const auto changed = nested ? next->session_.ToggleNestedMark(head.block, head.path, {first.offset, last.offset}, mark)
        : next->session_.ToggleMark({head.block, first.offset}, {tail.block, last.offset}, mark);
    if (!changed) return document;
    next->MapProjection();
    const auto paragraphs = document.Paragraphs();
    if (next->leaves_.size() != paragraphs.size())
      throw std::invalid_argument("Markdown format command changed the document structure");
    return editor::Document({paragraphs.begin(), paragraphs.end()}, std::move(next));
  }

private:
  struct Leaf {
    std::size_t block;
    std::vector<std::size_t> path;
    const MarkdownNode* node;
  };

  void MapProjection() {
    leaves_.clear();
    indexes_.clear();
    block_starts_.clear();
    for (std::size_t block = 0; block < session_.BlockCount(); ++block) {
      block_starts_.push_back(leaves_.size());
      const auto& root = session_.Block(block);
      const auto syntax = session_.SyntaxIndex(block);
      if (!syntax) {
        leaves_.push_back({block, {}, &root});
        continue;
      }
      for (auto& path : DocumentEditablePaths(session_.Document(), session_.Source(), *syntax)) {
        const auto* node = &root;
        for (const auto index : std::span(path).subspan(1)) node = &node->children[index];
        if (PlainText(*node).find_first_of("\r\n") != std::string::npos) continue;
        indexes_.emplace(node, leaves_.size());
        leaves_.push_back({block, std::move(path), node});
      }
    }
    block_starts_.push_back(leaves_.size());
  }

  bool IsNestedRange(std::size_t first, std::size_t last) const {
    const auto nested = leaves_[first].path.size() > 1;
    if (first == last) return nested;
    if (std::ranges::any_of(std::span(leaves_).subspan(first, last - first + 1),
                           [](const Leaf& leaf) { return leaf.path.size() > 1; }))
      throw std::invalid_argument("Use block or source editing to change several nested blocks");
    return false;
  }

  AttributedText ParagraphText(std::size_t index) const {
    return AttributedText(PlainText(*leaves_.at(index).node));
  }

  MarkdownDocumentEditSession session_;
  ParseOptions options_;
  std::vector<Leaf> leaves_;
  std::unordered_map<const MarkdownNode*, std::size_t> indexes_;
  std::vector<std::size_t> block_starts_;
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

rich_text::detail::EditorLayout EditableMarkdownLayout(const rich_text::detail::Document& document,
                                                       MarkdownStyle style) {
  return {Backing(document).ItemIds(document.Paragraphs()),
          [document](std::size_t index) { return Backing(document).ItemForParagraph(index); },
          [document, style = std::move(style)](std::size_t block, const rich_text::detail::RenderParagraph& render) {
            return Backing(document).RenderItem(block, style, render);
          }};
}

} // namespace huxerui::markdown::detail
