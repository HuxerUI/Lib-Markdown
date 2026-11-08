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

  View RenderItem(std::size_t block, const MarkdownOptions& options, const MarkdownStyle& style,
                  EventEmitter events, const editor::RenderParagraph& render) const {
    return RenderEditableBlock(session_.Block(block), options, style, std::move(events),
        [&](const MarkdownNode& node, TextStyle base, TextAlign align) -> std::optional<View> {
          auto index = block_starts_[block];
          if (session_.SyntaxIndex(block)) {
            const auto found = indexes_.find(&node);
            if (found == indexes_.end()) return std::nullopt;
            index = found->second;
          }
          const MarkdownRichText rich(node, options_);
          auto displayed = AttributedText::FromRanges(
              rich.Value().text, BuildEditableStyles(rich, base, style, options.link_policy));
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

  TextEditingValue Value(const editor::Document& document, TextSelection selection,
                         std::optional<TextRange> composition) const {
    auto result = TextEditingValue::FromText(std::string(Source()));
    const auto map = [&](TextOffset offset, TextAffinity affinity) {
      const auto position = document.Locate(offset);
      const auto byte = SourceByte(position.index, position.offset, affinity);
      return AttributedText(std::string(Source().substr(0, byte))).Length();
    };
    result.selection = {map(selection.anchor, selection.affinity),
                        map(selection.active, selection.affinity), selection.affinity};
    if (composition) {
      const auto start = map(composition->start, TextAffinity::Downstream);
      const auto end = composition->IsCollapsed() ? start : map(composition->end, TextAffinity::Upstream);
      result.composition = TextRange{start, std::max(start, end)};
    }
    return result;
  }

private:
  std::size_t SourceByte(std::size_t index, TextOffset offset, TextAffinity affinity) const {
    const auto& leaf = leaves_.at(index);
    const auto range = leaf.path.empty() ? session_.SourceRange(leaf.block)
                                         : session_.Document().SourceRange(leaf.path);
    if (!range) return 0;
    const auto plain_length = AttributedText(PlainText(*leaf.node)).Length();
    const auto conservative = [&] {
      return offset <= 0 ? range->start_byte : offset >= plain_length ? range->end_byte : range->start_byte;
    };
    if (!MarkdownRichText::Supports(*leaf.node)) return conservative();
    auto rich = leaf.path.size() > 1 ? RichProjection(*leaf.node, true, options_)
                                     : MarkdownRichText(*leaf.node, options_);
    auto displayed = rich.Value();
    displayed.selection = {offset, offset, affinity};
    rich.Update(displayed);
    const auto mapped = rich.SourceValue();
    if (mapped.text != Source().substr(range->start_byte, range->end_byte - range->start_byte))
      return conservative();
    return range->start_byte +
        AttributedText(mapped.text).TextInRange({0, mapped.selection.active}).size();
  }

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

TextEditingValue EditableMarkdownValue(const rich_text::detail::Document& document,
                                       TextSelection selection, std::optional<TextRange> composition) {
  return Backing(document).Value(document, selection, composition);
}

rich_text::detail::EditorLayout EditableMarkdownLayout(const rich_text::detail::Document& document,
                                                       MarkdownOptions options, MarkdownStyle style,
                                                       EventEmitter events) {
  options.selectable = false;
  return {Backing(document).ItemIds(document.Paragraphs()),
          [document](std::size_t index) { return Backing(document).ItemForParagraph(index); },
          [document, options = std::move(options), style = std::move(style), events = std::move(events)](
              std::size_t block, const rich_text::detail::RenderParagraph& render) mutable {
            return Backing(document).RenderItem(block, options, style, events, render);
          }};
}

[[huxerui::composable]]
View MarkdownDocumentEditor(std::shared_ptr<rich_text::detail::EditSession> edit,
                            MarkdownEditorOptions options,
                            std::function<void(const TextEditingValue&)> changed,
                            std::function<void(std::string_view)> rejected,
                            std::shared_ptr<rich_text::detail::InlineFormatActions> format_actions) {
  if (!edit) throw std::invalid_argument("Markdown document editor requires a session");
  const auto events = UseEvents();
  const auto style = options.markdown_options.style ? *options.markdown_options.style
                                                    : MarkdownStyle::FromTheme(UseTheme());
  return rich_text::detail::RichTextEditor(edit, {
      .base_style = style.body,
      .paragraph_spacing = style.block_spacing,
      .estimated_item_extent = options.virtualization.estimated_block_extent,
      .cache_extent = options.virtualization.cache_extent,
      .scroll_controller = options.virtualization.scroll_controller,
      .layout = [options = options.markdown_options, style, events](const rich_text::detail::Document& document) mutable {
        return EditableMarkdownLayout(document, options, style, events);
      },
      .changed = [changed = std::move(changed)](const rich_text::detail::Document& document,
                   TextSelection selection, std::optional<TextRange> composition) mutable {
        if (changed) changed(EditableMarkdownValue(document, selection, composition));
      },
      .rejected = std::move(rejected),
      .format_actions = std::move(format_actions),
  });
}

} // namespace huxerui::markdown::detail
