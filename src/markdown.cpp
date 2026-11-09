#include <huxerui/markdown.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <unordered_map>

#include "inline.h"

namespace huxerui::markdown {
namespace {

void ApplyInlineStyle(NodeKind kind, const MarkdownStyle& theme, TextSpanStyle& style) {
  switch (kind) {
  case NodeKind::Strong: style.font_weight = FontWeight::Bold; break;
  case NodeKind::Emphasis: style.font_slant = FontSlant::Italic; break;
  case NodeKind::Strikethrough:
    style.decoration = style.decoration.value_or(TextDecoration::None) | TextDecoration::StrikeThrough;
    break;
  case NodeKind::Underline:
  case NodeKind::Insertion:
    style.decoration = style.decoration.value_or(TextDecoration::None) | TextDecoration::Underline;
    break;
  case NodeKind::Highlight:
    style.foreground = theme.highlight_foreground;
    style.background = theme.highlight_background;
    break;
  case NodeKind::Code:
  case NodeKind::InlineMath:
  case NodeKind::DisplayMath:
    style.font = theme.code.font;
    style.foreground = theme.code.foreground;
    style.background = theme.code_background;
    break;
  case NodeKind::Link:
  case NodeKind::WikiLink:
  case NodeKind::FootnoteReference:
    style.foreground = theme.link;
    style.decoration = style.decoration.value_or(TextDecoration::None) | TextDecoration::Underline;
    break;
  default: break;
  }
}

bool AllowsLinkDestination(std::string_view destination, const MarkdownLinkPolicy& policy) {
  return policy ? policy(destination) : IsDefaultLinkAllowed(destination);
}

struct InlineBuilder {
  const MarkdownStyle& theme;
  const MarkdownLinkPolicy& policy;
  std::string text;
  std::vector<TextStyleRange> styles;
  std::vector<TextLinkRange> links;
  std::vector<detail::OriginalLink> originals;
  TextOffset length = 0;
  const std::set<unsigned>* revealed_spoilers = nullptr;
  unsigned spoiler_index = 0;
  unsigned concealed_depth = 0;

  void Append(const std::string& value, const TextSpanStyle& style) {
    if (value.empty()) return;
    const auto begin = length;
    length += AttributedText(value).Length();
    text += value;
    auto resolved = style;
    if (concealed_depth) resolved.foreground = resolved.background = theme.spoiler_cover;
    styles.push_back({{begin, length}, resolved});
  }

  void Visit(const MarkdownNode& node, TextSpanStyle style = {}) {
    switch (node.kind) {
    case NodeKind::Spoiler: {
      const auto index = ++spoiler_index;
      const auto begin = length;
      const bool concealed = !revealed_spoilers || !revealed_spoilers->contains(index);
      const bool outer = concealed_depth == 0;
      if (concealed) ++concealed_depth;
      Append(node.text, style);
      for (const auto& child : node.children) Visit(child, style);
      if (concealed) --concealed_depth;
      if (concealed && outer && length > begin) {
        links.push_back({{begin, length}, Uri("huxerui-markdown:spoiler-" + std::to_string(index))});
        originals.push_back({.range = {begin, length}, .kind = NodeKind::Spoiler, .spoiler_index = index});
      }
      return;
    }
    case NodeKind::Image:
      style.foreground = theme.muted;
      Append(PlainText(node), style);
      return;
    case NodeKind::WikiLink:
    case NodeKind::FootnoteReference:
    case NodeKind::Link: {
      const bool allowed = !concealed_depth &&
          (node.kind != NodeKind::Link || AllowsLinkDestination(node.destination, policy));
      const auto begin = length;
      if (allowed) ApplyInlineStyle(node.kind, theme, style);
      Append(node.text, style);
      for (const auto& child : node.children) Visit(child, style);
      if (allowed && length > begin) {
        auto uri = node.kind == NodeKind::Link ? Uri::Parse(node.destination) : std::optional<Uri>{};
        // Uri requires an absolute ASCII URI. Keep relative/Unicode originals
        // separately and use an inert identity for the text interaction.
        if (!uri) uri = Uri("huxerui-markdown:link-" + std::to_string(originals.size()));
        links.push_back({{begin, length}, std::move(*uri)});
        originals.push_back({{begin, length}, node.destination, node.kind,
                             node.footnote_number, node.footnote_label, node.footnote_reference});
      }
      return;
    }
    default: ApplyInlineStyle(node.kind, theme, style); break;
    }
    Append(node.text, style);
    for (const auto& child : node.children) Visit(child, style);
  }
};

void ValidateStyle(const MarkdownStyle& theme) {
  for (const auto size : theme.heading_sizes)
    if (!std::isfinite(size) || size <= 0) throw std::invalid_argument("Markdown heading sizes must be positive and finite");
  for (const auto value : {theme.block_spacing, theme.list_spacing, theme.indent, theme.code_padding,
                           theme.corner_radius, theme.blank_line_height})
    if (!std::isfinite(value) || value < 0) throw std::invalid_argument("Markdown dimensions must be nonnegative and finite");
  if (!std::isfinite(theme.table_cell_width) || theme.table_cell_width <= 0)
    throw std::invalid_argument("Markdown table_cell_width must be positive and finite");
}

float BlockGap(const MarkdownNode& before, const MarkdownNode& after, float spacing) {
  return before.kind == NodeKind::BlankLines || after.kind == NodeKind::BlankLines ? 0.0F : spacing;
}

View BlockColumn(std::vector<View> blocks, const std::vector<MarkdownNode>& nodes, float spacing) {
  const bool explicit_blanks = std::any_of(nodes.begin(), nodes.end(),
      [](const MarkdownNode& node) { return node.kind == NodeKind::BlankLines; });
  if (!explicit_blanks)
    return Column(std::move(blocks)).With(Spacing(spacing), CrossAlign(CrossAxisAlignment::Stretch));
  std::vector<View> children;
  for (std::size_t i = 0; i < blocks.size(); ++i) {
    if (i) {
      const auto gap = BlockGap(nodes[i - 1], nodes[i], spacing);
      if (gap > 0) children.push_back(Column(std::vector<View>{}).With(Frame{.height = gap}));
    }
    children.push_back(std::move(blocks[i]));
  }
  return Column(std::move(children)).With(CrossAlign(CrossAxisAlignment::Stretch));
}

bool HasBlankLines(const MarkdownNode& node) {
  if (node.kind == NodeKind::BlankLines) return true;
  return std::any_of(node.children.begin(), node.children.end(), HasBlankLines);
}

bool HasSpoilers(const MarkdownNode& node) {
  return node.kind == NodeKind::Spoiler || std::any_of(node.children.begin(), node.children.end(), HasSpoilers);
}

class Renderer {
public:
  Renderer(const MarkdownOptions& options, MarkdownStyle theme, EventEmitter events,
           const std::unordered_map<const MarkdownNode*, TextBlockId>* selection = nullptr,
           bool interactive = true)
      : options_(options), theme_(std::move(theme)), events_(std::move(events)), selection_(selection),
        interactive_(interactive) {}

  View RenderBlock(const MarkdownNode& node) const { return Block(node); }
  View RenderInline(const MarkdownNode& node, TextStyle style, TextAlign alignment) const {
    return Paragraph(node, std::move(style), alignment);
  }

  View Blocks(const MarkdownNode& parent, float spacing) const {
    std::vector<View> children;
    for (const auto& node : parent.children) children.push_back(Block(node));
    return BlockColumn(std::move(children), parent.children, spacing);
  }

private:
  Text BindSelection(Text text, const MarkdownNode& node) const {
    if (selection_) return std::move(text).SelectionBlock(selection_->at(&node));
    return text;
  }

  View Paragraph(const MarkdownNode& node, TextStyle style, TextAlign alignment = TextAlign::Leading) const {
    if (node.kind == NodeKind::Paragraph && node.children.size() == 1) {
      const auto& child = node.children.front();
      if (options_.standalone_image_renderer && child.kind == NodeKind::Image)
        return options_.standalone_image_renderer(child);
      if (options_.standalone_math_renderer && (child.kind == NodeKind::InlineMath || child.kind == NodeKind::DisplayMath))
        return options_.standalone_math_renderer(child);
    }
    if (HasSpoilers(node)) {
      const auto selection_id = selection_ ? std::optional<TextBlockId>{selection_->at(&node)} : std::nullopt;
      return Scope([owned = node, style, alignment, theme = theme_, policy = options_.link_policy,
                    events = events_, interactive = interactive_, selection_id]() -> View {
        auto revealed = UseState(std::set<unsigned>{});
        auto previous = UseState(owned);
        if (previous != owned) { previous = owned; revealed = std::set<unsigned>{}; }
        auto content = detail::BuildInline(owned, theme, policy, &revealed.Get());
        auto attributed = content.text;
        if (!interactive) {
          const auto ranges = attributed.StyleRanges();
          attributed = AttributedText::FromRanges(attributed.PlainText(), {ranges.begin(), ranges.end()});
        }
        auto text = Text(std::move(attributed)).Style(style).Align(alignment);
        if (selection_id) text = std::move(text).SelectionBlock(*selection_id);
        return std::move(text).On<TextEvents::LinkActivated>([revealed, events, links = std::move(content.links)](const TextLinkActivation& event) mutable {
          for (const auto& link : links) {
            if (link.range != event.range) continue;
            if (link.kind == NodeKind::Spoiler) {
              auto next = revealed.Get(); next.insert(link.spoiler_index); revealed = std::move(next);
            } else if (link.kind == NodeKind::WikiLink) events.Emit<MarkdownEvents::WikiLinkActivated>(MarkdownWikiLinkActivation{link.destination});
            else if (link.kind == NodeKind::FootnoteReference)
              events.Emit<MarkdownEvents::FootnoteActivated>(MarkdownFootnoteActivation{link.footnote_number, link.footnote_label, link.footnote_reference});
            else events.Emit<MarkdownEvents::LinkActivated>(MarkdownLinkActivation{link.destination});
            return;
          }
        });
      });
    }
    auto inline_text = detail::BuildInline(node, theme_, options_.link_policy);
    if (!interactive_) {
      const auto styles = inline_text.text.StyleRanges();
      auto text = AttributedText::FromRanges(inline_text.text.PlainText(), {styles.begin(), styles.end()});
      return Text(std::move(text)).Style(std::move(style)).Align(alignment);
    }
    return BindSelection(Text(std::move(inline_text.text)).Style(std::move(style)).Align(alignment), node)
        .On<TextEvents::LinkActivated>([events = events_, links = std::move(inline_text.links)](const TextLinkActivation& event) {
          for (const auto& link : links) {
            if (link.range == event.range) {
              if (link.kind == NodeKind::WikiLink) {
                events.Emit<MarkdownEvents::WikiLinkActivated>(MarkdownWikiLinkActivation{link.destination});
              } else if (link.kind == NodeKind::FootnoteReference) {
                events.Emit<MarkdownEvents::FootnoteActivated>(MarkdownFootnoteActivation{
                    link.footnote_number, link.footnote_label, link.footnote_reference});
              } else {
                events.Emit<MarkdownEvents::LinkActivated>(MarkdownLinkActivation{link.destination});
              }
              return;
            }
          }
        });
  }

  View List(const MarkdownNode& node) const {
    std::vector<View> rows;
    std::uint64_t number = node.list_start;
    for (const auto& item : node.children) {
      std::string marker;
      if (item.task_checked) marker = *item.task_checked ? "\xE2\x98\x91" : "\xE2\x98\x90";
      else marker = node.kind == NodeKind::OrderedList ? std::to_string(number) + "." : "\xE2\x80\xA2";
      ++number;
      auto content = Blocks(item, node.tight ? theme_.list_spacing : theme_.block_spacing);
      rows.push_back(Row{
        Text(std::move(marker)).Style(theme_.body).With(Frame{.min_width = theme_.indent}),
        std::move(content).With(Grow{}),
      }.With(Spacing(6.0F), CrossAlign(CrossAxisAlignment::Start)));
    }
    return Column(std::move(rows)).With(Spacing(theme_.list_spacing), CrossAlign(CrossAxisAlignment::Stretch));
  }

  View Table(const MarkdownNode& node) const {
    std::vector<View> rows;
    for (const auto& section : node.children) {
      for (const auto& row : section.children) {
        std::vector<View> cells;
        for (const auto& cell : row.children) {
          auto style = theme_.body;
          const bool header = cell.kind == NodeKind::TableHeaderCell;
          if (header) style.font = style.font.WithWeight(FontWeight::Bold);
          auto alignment = TextAlign::Leading;
          if (cell.alignment == CellAlignment::Center) alignment = TextAlign::Center;
          if (cell.alignment == CellAlignment::Right) alignment = TextAlign::Trailing;
          View view = Paragraph(cell, style, alignment)
              .With(Frame{.width = theme_.table_cell_width}, Padding(8.0F), Border{theme_.border, 0.5F});
          if (header) view = std::move(view).With(Background{theme_.table_header_background});
          cells.push_back(std::move(view));
        }
        rows.push_back(Row(std::move(cells)).With(CrossAlign(CrossAxisAlignment::Stretch)));
      }
    }
    return ScrollView(Column(std::move(rows))).ScrollAxis(Axis::Horizontal);
  }

  View Block(const MarkdownNode& node) const {
    switch (node.kind) {
    case NodeKind::Heading: {
      auto style = theme_.body;
      const auto level = std::clamp(node.heading_level, 1U, 6U);
      style.font = style.font.WithSize(theme_.heading_sizes[level - 1]).WithWeight(FontWeight::Bold);
      return Paragraph(node, std::move(style));
    }
    case NodeKind::Paragraph: return Paragraph(node, theme_.body);
    case NodeKind::BlankLines: {
      const auto height = static_cast<float>(node.blank_line_count) * theme_.blank_line_height;
      if (!std::isfinite(height)) throw std::invalid_argument("Markdown blank line extent overflow");
      return Column(std::vector<View>{}).With(Frame{.height = height});
    }
    case NodeKind::CodeBlock:
      if (options_.code_block_renderer) return options_.code_block_renderer(node);
      return BindSelection(Text(node.text).Style(theme_.code), node)
          .With(Padding(theme_.code_padding), Background{theme_.code_background}, CornerRadius(theme_.corner_radius));
    case NodeKind::BlockQuote:
      return Blocks(node, theme_.block_spacing)
          .With(Padding(EdgeInsets{8.0F, 8.0F, 8.0F, theme_.indent}),
                Background{theme_.quote_background}, Border{theme_.border, 1.0F});
    case NodeKind::Admonition: {
      if (options_.admonition_renderer) return options_.admonition_renderer(node);
      const char* label = "Note";
      switch (node.admonition_kind) {
      case AdmonitionKind::Tip: label = "Tip"; break;
      case AdmonitionKind::Important: label = "Important"; break;
      case AdmonitionKind::Warning: label = "Warning"; break;
      case AdmonitionKind::Caution: label = "Caution"; break;
      default: break;
      }
      return Column{
        Text(label).Style({theme_.body.font.WithWeight(FontWeight::Bold), theme_.admonition_foreground}),
        Blocks(node, theme_.block_spacing),
      }.With(Padding(theme_.code_padding), Spacing(theme_.list_spacing), CrossAlign(CrossAxisAlignment::Stretch),
             Background{theme_.admonition_background}, Border{theme_.admonition_foreground, 1.0F}, CornerRadius(theme_.corner_radius));
    }
    case NodeKind::FootnoteSection:
      return Column{
        Column(std::vector<View>{}).With(Frame{.height = 1.0F}, Background{theme_.border}),
        Blocks(node, theme_.block_spacing),
      }.With(Spacing(theme_.block_spacing), CrossAlign(CrossAxisAlignment::Stretch));
    case NodeKind::FootnoteDefinition:
      return Row{
        Text("[" + std::to_string(node.footnote_number) + "]").Style({theme_.body.font, theme_.muted}),
        Blocks(node, theme_.block_spacing).With(Grow{}),
      }.With(Spacing(8.0F), CrossAlign(CrossAxisAlignment::Start));
    case NodeKind::UnorderedList:
    case NodeKind::OrderedList: return List(node);
    case NodeKind::ThematicBreak:
      return Column(std::vector<View>{}).With(Frame{.height = 1.0F}, Background{theme_.border});
    case NodeKind::Table: return Table(node);
    case NodeKind::Document:
    case NodeKind::ListItem: return Blocks(node, theme_.block_spacing);
    default: return Paragraph(node, theme_.body);
    }
  }

  const MarkdownOptions& options_;
  MarkdownStyle theme_;
  EventEmitter events_;
  const std::unordered_map<const MarkdownNode*, TextBlockId>* selection_;
  bool interactive_;
};

// Owned block records avoid retaining an entire historical AST for each reused
// block. Text bodies and records are shared between logical selection snapshots.
struct VirtualBlock {
  MarkdownNode node;
  std::uint64_t id;
  std::unordered_map<const MarkdownNode*, TextBlockId> bindings;
  std::vector<TextBlockId> ids;
  std::vector<TextSelectionBlock> paragraphs;

  VirtualBlock(const MarkdownNode& value, std::uint64_t& next_id)
      : node(value), id(++next_id) {
    Collect(node, next_id);
  }

  void Collect(const MarkdownNode& value, std::uint64_t& next_id) {
    if (value.kind == NodeKind::BlankLines) {
      if (!paragraphs.empty()) paragraphs.back().separator.append(value.blank_line_count, '\n');
      return;
    }
    switch (value.kind) {
    case NodeKind::Paragraph:
    case NodeKind::Heading:
    case NodeKind::TableCell:
    case NodeKind::TableHeaderCell:
    case NodeKind::CodeBlock: {
      const auto text_id = ++next_id;
      bindings.emplace(&value, text_id);
      ids.push_back(text_id);
      paragraphs.push_back({AttributedText(PlainText(value)), "\n"});
      return;
    }
    default:
      for (const auto& child : value.children) Collect(child, next_id);
    }
  }
};

class VirtualSnapshot final : public TextSelectionSource {
public:
  MarkdownDocument document;
  std::vector<std::shared_ptr<const VirtualBlock>> blocks;

  VirtualSnapshot(MarkdownDocument value, const std::shared_ptr<const VirtualSnapshot>& previous,
                  std::uint64_t& next_id) : document(std::move(value)) {
    const auto& nodes = document.Root().children;
    blocks.resize(nodes.size());
    std::size_t prefix = 0, suffix = 0;
    if (previous) {
      const auto& old = previous->blocks;
      while (prefix < nodes.size() && prefix < old.size() && old[prefix]->node == nodes[prefix]) {
        blocks[prefix] = old[prefix];
        ++prefix;
      }
      while (suffix < nodes.size() - prefix && suffix < old.size() - prefix &&
             old[old.size() - suffix - 1]->node == nodes[nodes.size() - suffix - 1]) {
        blocks[nodes.size() - suffix - 1] = old[old.size() - suffix - 1];
        ++suffix;
      }
    }
    for (auto i = prefix; i < nodes.size() - suffix; ++i)
      blocks[i] = std::make_shared<const VirtualBlock>(nodes[i], next_id);
    for (const auto& block : blocks) {
      if (block->node.kind == NodeKind::BlankLines && !positions_.empty())
        positions_.back().separator.append(block->node.blank_line_count, '\n');
      for (std::size_t i = 0; i < block->ids.size(); ++i) {
        indexes_.emplace(block->ids[i], positions_.size());
        positions_.push_back({block, i, block->paragraphs[i].separator});
      }
    }
  }

  std::size_t Count() const noexcept override { return positions_.size(); }
  TextBlockId IdAt(std::size_t index) const override {
    const auto& position = positions_.at(index);
    return position.block->ids[position.paragraph];
  }
  std::optional<std::size_t> IndexOf(TextBlockId id) const override {
    const auto found = indexes_.find(id);
    return found == indexes_.end() ? std::nullopt : std::optional<std::size_t>(found->second);
  }
  TextSelectionBlock BlockAt(std::size_t index) const override {
    const auto& position = positions_.at(index);
    return {position.block->paragraphs[position.paragraph].text, position.separator};
  }

private:
  struct Position {
    std::shared_ptr<const VirtualBlock> block;
    std::size_t paragraph;
    std::string separator;
  };
  std::vector<Position> positions_;
  std::unordered_map<TextBlockId, std::size_t> indexes_;
};

struct VirtualState {
  std::shared_ptr<const VirtualSnapshot> snapshot;
  std::uint64_t next_id = 0;
};

} // namespace

namespace detail {
InlineResult BuildInline(const MarkdownNode& node, const MarkdownStyle& theme,
                         const MarkdownLinkPolicy& policy, const std::set<unsigned>* revealed_spoilers) {
  InlineBuilder builder{theme, policy, {}, {}, {}, {}, 0};
  builder.revealed_spoilers = revealed_spoilers;
  builder.Visit(node);
  return {AttributedText::FromRanges(std::move(builder.text), std::move(builder.styles), std::move(builder.links)),
          std::move(builder.originals)};
}

} // namespace detail

MarkdownStyle MarkdownStyle::FromTheme(const ThemeSpec& theme) {
  MarkdownStyle result;
  result.body = {Font::FromFamily(theme.typography.family, theme.typography.body_large), theme.colors.on_surface};
  result.code = {Font::Monospace(theme.typography.body_medium), theme.colors.on_surface};
  result.link = theme.colors.primary;
  result.muted = theme.colors.on_surface_variant;
  result.code_background = theme.colors.surface_container_highest;
  result.quote_background = theme.colors.surface_container;
  result.table_header_background = theme.colors.surface_container_highest;
  result.border = theme.colors.outline;
  result.highlight_foreground = theme.colors.on_primary_container;
  result.highlight_background = theme.colors.primary_container;
  result.spoiler_cover = theme.colors.on_surface_variant;
  result.admonition_foreground = theme.colors.primary;
  result.admonition_background = theme.colors.surface_container;
  result.blank_line_height = theme.typography.body_large * 1.25F;
  const float scale = theme.typography.body_large / 16.0F;
  for (auto& size : result.heading_sizes) size *= scale;
  return result;
}

View Markdown(std::string_view source, MarkdownOptions options) {
  auto document = ParseMarkdown(source);
  return Markdown(std::move(document), std::move(options));
}

View Markdown(MarkdownDocument document, MarkdownOptions options) {
  if (options.style) ValidateStyle(*options.style);
  return Scope([document = std::move(document), options = std::move(options)] {
    const auto theme = options.style ? *options.style : MarkdownStyle::FromTheme(UseTheme());
    ValidateStyle(theme);
    const auto events = UseEvents();
    // Explicit blank nodes have no Text endpoint. Supply logical separators so
    // selection/copy preserves their interior newlines in ordinary views as well.
    if (options.selectable && HasBlankLines(document.Root())) {
      auto state = UseState(std::make_shared<VirtualState>()).Get();
      if (!state->snapshot || state->snapshot->document != document)
        state->snapshot = std::make_shared<const VirtualSnapshot>(document, state->snapshot, state->next_id);
      const auto snapshot = state->snapshot;
      std::vector<View> children;
      for (const auto& block : snapshot->blocks)
        children.push_back(Renderer(options, theme, events, &block->bindings).RenderBlock(block->node));
      return View(SelectionArea(BlockColumn(std::move(children), document.Root().children, theme.block_spacing)).Source(snapshot));
    }
    auto content = Renderer(options, theme, events).Blocks(document.Root(), theme.block_spacing);
    if (options.selectable) return View(SelectionArea(std::move(content)));
    return content;
  });
}

View VirtualizedMarkdown(std::string_view source, MarkdownOptions options,
                         MarkdownVirtualizationOptions virtualization) {
  return VirtualizedMarkdown(ParseMarkdown(source), std::move(options), std::move(virtualization));
}

View VirtualizedMarkdown(MarkdownDocument document, MarkdownOptions options,
                         MarkdownVirtualizationOptions virtualization) {
  if (options.style) ValidateStyle(*options.style);
  if (!std::isfinite(virtualization.estimated_block_extent) || virtualization.estimated_block_extent <= 0 ||
      !std::isfinite(virtualization.cache_extent) || virtualization.cache_extent < 0)
    throw std::invalid_argument("Invalid Markdown virtualization extents");
  return Scope([document = std::move(document), options = std::move(options), virtualization] {
    const auto theme = options.style ? *options.style : MarkdownStyle::FromTheme(UseTheme());
    ValidateStyle(theme);
    auto state = UseState(std::make_shared<VirtualState>()).Get();
    if (!state->snapshot || state->snapshot->document != document)
      state->snapshot = std::make_shared<const VirtualSnapshot>(document, state->snapshot, state->next_id);
    const auto snapshot = state->snapshot;
    const auto events = UseEvents();
    auto list = VirtualList(snapshot->blocks.size(), [snapshot, options, theme, events](std::size_t index) -> View {
      const auto& block = snapshot->blocks[index];
      // Deferred item composition is essential: only realized rows build Text and
      // invoke render callbacks, even if the SDK inspects all item keys.
      // SDK measured extents ignore zero. A subpixel floor lets empty/collapsed
      // rows replace their old estimate instead of retaining an obsolete gap.
      return Scope([block, options, theme, events] {
        return Renderer(options, theme, events, options.selectable ? &block->bindings : nullptr)
            .RenderBlock(block->node);
      }).With(Frame{.min_height = 0.001F}, Padding(EdgeInsets{0, 0, index + 1 < snapshot->blocks.size() ?
          BlockGap(block->node, snapshot->blocks[index + 1]->node, theme.block_spacing) : 0, 0}))
        .Key(block->id);
    }).EstimatedItemExtent(virtualization.estimated_block_extent).CacheExtent(virtualization.cache_extent);
    if (virtualization.scroll_controller) list = std::move(list).Controller(*virtualization.scroll_controller);
    if (options.selectable) return View(SelectionArea(std::move(list)).Source(snapshot));
    return View(std::move(list));
  });
}

} // namespace huxerui::markdown
