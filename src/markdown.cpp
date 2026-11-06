#include <huxerui/markdown.h>
#include <huxerui/animation.h>
#include <huxerui/task.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <unordered_map>

#include "inline.h"
#include "editing_internal.h"

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
        // Native Uri requires an absolute ASCII URI. Keep relative/Unicode originals
        // separately and use an inert identity for the native text interaction.
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
  using TextEditor = std::function<std::optional<View>(const MarkdownNode&, TextStyle, TextAlign)>;
  Renderer(const MarkdownOptions& options, MarkdownStyle theme, EventEmitter events,
           const std::unordered_map<const MarkdownNode*, TextBlockId>* selection = nullptr,
           bool interactive = true, TextEditor editor = {})
      : options_(options), theme_(std::move(theme)), events_(std::move(events)), selection_(selection),
        interactive_(interactive), editor_(std::move(editor)) {}

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
    if (editor_) {
      if (auto input = editor_(node, style, alignment)) return std::move(*input);
    }
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
        if (previous.Get() != owned) { previous = owned; revealed = std::set<unsigned>{}; }
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
  TextEditor editor_;
};

// Owned block records avoid retaining an entire historical AST for each reused
// block. Text bodies and records are shared between logical selection snapshots.
struct VirtualBlock {
  MarkdownNode node;
  std::uint64_t id;
  std::unordered_map<const MarkdownNode*, TextBlockId> bindings;
  std::vector<TextBlockId> ids;
  std::vector<TextSelectionBlock> paragraphs;

  VirtualBlock(const MarkdownNode& value, std::uint64_t& next_id, std::optional<std::uint64_t> existing_id = {})
      : node(value), id(existing_id ? *existing_id : ++next_id) {
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
                  std::uint64_t& next_id, std::optional<std::size_t> retained_row = {}) : document(std::move(value)) {
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
    for (auto i = prefix; i < nodes.size() - suffix; ++i) {
      const auto id = previous && nodes.size() == previous->blocks.size() && retained_row == i
          ? std::optional{previous->blocks[i]->id} : std::nullopt;
      blocks[i] = std::make_shared<const VirtualBlock>(nodes[i], next_id, id);
    }
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

struct EditingPresentation { using Value = bool; };

bool EmptyForEditing(const MarkdownDocument& document) {
  const auto& children = document.Root().children;
  return std::all_of(children.begin(), children.end(), [](const MarkdownNode& node) { return node.kind == NodeKind::BlankLines; });
}

// The same TextField stays mounted while its presentation changes. In reading
// presentation it is a transparent input backing beneath noninteractive Markdown;
// a real pointer/Tab focus enters the native client without synthetic input or a
// second click. Exact glyph-to-source caret mapping is deliberately not promised.
class EditableBlockLayout final : public Layout<EditableBlockLayout> {
public:
  using Layout::Layout;
  static LayoutResult Measure(LayoutContext& context, ViewNode& node, Constraints constraints) {
    auto& input = node.ChildAt(0);
    auto& preview = node.ChildAt(1);
    const bool editing = node.LayoutValueOr<EditingPresentation>(false);
    const auto rendered = context.Measure(preview, constraints.LooseHeight());
    const auto field = context.Measure(input, editing ? constraints.LooseHeight() :
        constraints.LooseHeight().TightHeight(std::max(24.0F, rendered.height)));
    LayoutResult result;
    result.Place(input, {0, 0}).Place(preview, {0, editing ? field.height + node.Spacing() : 0});
    return result.SetSize(constraints.Constrain({std::max(field.width, rendered.width),
        editing ? field.height + node.Spacing() + rendered.height : std::max(field.height, rendered.height)}));
  }
};

using NodePath = std::vector<std::size_t>;
using EditablePaths = std::unordered_map<const MarkdownNode*, NodePath>;

const MarkdownNode* NodeAt(const MarkdownNode& root, std::span<const std::size_t> path) {
  const auto* node = &root;
  for (const auto index : path) {
    if (index >= node->children.size()) return nullptr;
    node = &node->children[index];
  }
  return node;
}

bool CollectEditablePaths(const MarkdownDocument& document, std::string_view source, bool rich_text,
                          const MarkdownNode& node, NodePath& path, EditablePaths& paths) {
  if (node.kind == NodeKind::Paragraph || node.kind == NodeKind::Heading || detail::IsTableCell(node.kind)) {
    const auto range = document.SourceRange(path);
    if (!range) return false;
    if (path.size() > 1 && source.substr(range->start_byte, range->end_byte - range->start_byte)
                               .find_first_of("\r\n") != std::string_view::npos &&
        (!rich_text || !MarkdownRichText::Supports(node) || PlainText(node).find_first_of("\r\n") != std::string::npos))
      return false;
    paths.emplace(&node, path);
    return true;
  }
  if (node.kind == NodeKind::CodeBlock) return false;
  if (node.kind == NodeKind::ListItem && std::all_of(node.children.begin(), node.children.end(),
      [](const auto& child) { return child.kind == NodeKind::BlankLines; })) return false;
  bool complete = true;
  for (std::size_t i = 0; i < node.children.size(); ++i) {
    path.push_back(i);
    if (!CollectEditablePaths(document, source, rich_text, node.children[i], path, paths)) complete = false;
    path.pop_back();
  }
  return complete;
}

bool FindEditPath(const MarkdownDocument& document, const MarkdownNode& node, NodePath& path,
                  std::size_t start, NodeKind kind, bool nested) {
  if (!path.empty() && node.kind == kind && (path.size() > 1) == nested) {
    const auto range = document.SourceRange(path);
    if (range && range->start_byte == start) return true;
  }
  if ((!nested && !path.empty()) || node.kind == NodeKind::Paragraph || node.kind == NodeKind::Heading ||
      detail::IsTableCell(node.kind)) return false;
  for (std::size_t i = 0; i < node.children.size(); ++i) {
    path.push_back(i);
    if (FindEditPath(document, node.children[i], path, start, kind, nested)) return true;
    path.pop_back();
  }
  return false;
}

struct BlockEditorState {
  ParseOptions parse_options;
  MarkdownBlockEditSession session;
  VirtualState layout;
  MarkdownDocument fragment;
  std::string fragment_text;
  std::optional<MarkdownRichText> rich;
  std::optional<std::size_t> retained_row;
  bool rich_text;
  std::uint64_t focus_epoch = 0;
  bool empty_mode;
  explicit BlockEditorState(const TextEditingValue& value, ParseOptions options, bool rich_enabled)
      : parse_options(options), session(value.text, options), rich_text(rich_enabled), empty_mode(EmptyForEditing(session.Document())) {}

  void EndEdit() {
    if (rich && rich->Value().composition) throw std::logic_error("Finish IME composition before ending rich text editing");
    const auto path = session.EditingPath();
    const auto keep = path.size() > 1 && session.LayoutDocument() != session.Document()
        ? std::optional{path.front()} : std::nullopt;
    session.EndEdit();
    if (keep) retained_row = keep;
    rich.reset();
  }

  void UpdateEdit(const TextEditingValue& value) {
    if (!rich) { session.UpdateEdit(value); return; }
    auto candidate = *rich;
    candidate.Update(value);
    if (value.text != rich->Value().text || value.composition != rich->Value().composition)
      session.UpdateEdit(candidate.SourceValue());
    else {
      try {
        const auto mapped = candidate.SourceValue();
        if (mapped.text == session.EditingValue().text) session.UpdateEdit(mapped);
      } catch (const std::invalid_argument&) { /* Selection alone never normalizes source. */ }
        catch (const std::length_error&) { /* Canonical spelling can exceed a tight source limit. */ }
    }
    rich = std::move(candidate);
  }

  bool IsActive(const MarkdownDocument& layout_document, std::span<const std::size_t> path) const {
    return session.LayoutDocument() == layout_document && std::ranges::equal(session.EditingPath(), path);
  }

  bool Activate(const MarkdownDocument& layout_document, const NodePath& path) {
    if (session.LayoutDocument() != layout_document) return false; // Stale realized row.
    if (IsActive(layout_document, path)) return true;
    const auto range = layout_document.SourceRange(path);
    if (!range) return false;
    auto start = range->start_byte;
    if (const auto active = session.EditingPath(); !active.empty()) {
      const auto old = *layout_document.SourceRange(active);
      if (start >= old.end_byte)
        start = start - (old.end_byte - old.start_byte) + session.EditingValue().text.size();
      EndEdit();
    }
    const auto& document = session.Document();
    const auto kind = NodeAt(layout_document.Root(), path)->kind;
    auto target = path;
    const auto* node = NodeAt(document.Root(), target);
    const auto same_range = node && node->kind == kind ? document.SourceRange(target) : std::nullopt;
    if (!same_range || same_range->start_byte != start) {
      target.clear();
      if (!FindEditPath(document, document.Root(), target, start, kind, path.size() > 1)) return false;
      node = NodeAt(document.Root(), target);
    }
    std::optional<MarkdownRichText> projection;
    if (rich_text && MarkdownRichText::Supports(*node))
      projection = detail::RichProjection(*node, target.size() > 1, parse_options);
    session.BeginEdit(target);
    rich = std::move(projection);
    ++focus_epoch;
    return true;
  }

  TextEditingValue WholeValue() const {
    auto result = TextEditingValue::FromText(std::string(session.Source()));
    if (const auto path = session.EditingPath(); !path.empty()) {
      const auto range = *session.LayoutDocument().SourceRange(path);
      const auto prefix = AttributedText(std::string(session.Source().substr(0, range.start_byte))).Length();
      const auto& local = session.EditingValue();
      result.selection = {prefix + local.selection.anchor, prefix + local.selection.active, local.selection.affinity};
      if (local.composition) result.composition = TextRange{prefix + local.composition->start, prefix + local.composition->end};
    }
    return result;
  }
};

struct EditorRenderContext {
  std::shared_ptr<BlockEditorState> state;
  MarkdownDocument layout_document;
  MarkdownOptions render_options;
  MarkdownStyle theme;
  EventEmitter events;
  TaskScope tasks;
  State<std::uint64_t> revision;
  bool live_preview;
};

View EditableField(const EditorRenderContext& context, std::shared_ptr<const VirtualBlock> block,
                   NodePath path, TextStyle base, TextAlign alignment) {
  const auto range = *context.layout_document.SourceRange(path);
  auto source = std::string(context.state->session.LayoutSource().substr(range.start_byte, range.end_byte - range.start_byte));
  return Scope([context, block = std::move(block), path = std::move(path), base = std::move(base), alignment,
                source = std::move(source)]() -> View {
    const auto& [state, layout_document, render_options, theme, events, tasks, revision, live_preview] = context;
    const auto report = [events](const std::exception& error) {
      events.Emit<MarkdownEditorEvents::Error>(std::string(error.what()));
    };
    const auto& node = *NodeAt(block->node, std::span(path).subspan(1));
    auto input_generation = UseState(std::uint64_t{0});
    const auto generation = input_generation.Get();
    const bool active = state->IsActive(layout_document, path);
    auto local = active ? state->session.EditingValue() : TextEditingValue::FromText(source);
    std::optional<MarkdownRichText> initial_projection;
    if (!active && state->rich_text && MarkdownRichText::Supports(node))
      initial_projection = detail::RichProjection(node, path.size() > 1, state->parse_options);
    const auto& projection = active ? state->rich : initial_projection;
    if (projection) local = projection->Value();
    const bool table_cell = detail::IsTableCell(node.kind);
    auto input = TextField(local).Variant(TextFieldVariant::Standard)
        .LineLimits(TextFieldLineLimits::MultiLine(1))
        .Align(alignment).VerticalAlign(TextVerticalAlign::Top)
        .OnChanged([=](const TextEditingValue& next) mutable {
          if (input_generation.Get() != generation) return;
          bool activated = false;
          try {
            activated = state->Activate(layout_document, path);
            if (!activated) return;
            state->UpdateEdit(next);
            const auto whole = state->WholeValue();
            events.Emit<MarkdownEditorEvents::Changed>(whole);
          } catch (const std::exception& error) {
            // A rejected controlled proposal may still live in TextField's
            // native client. Retire that client rather than displaying text
            // which was never accepted into the document.
            input_generation = generation + 1;
            if (activated) {
              if (state->rich) {
                auto accepted = state->rich->Value(); accepted.composition.reset(); state->rich->Update(accepted);
              }
              auto accepted = state->session.EditingValue(); accepted.composition.reset();
              state->session.UpdateEdit(accepted);
              const auto whole = state->WholeValue(); events.Emit<MarkdownEditorEvents::Changed>(whole);
              state->EndEdit(); ++state->focus_epoch;
            }
            report(error);
          }
          revision = revision.Get() + 1;
        }).On<ViewEvents::FocusChanged>([=](bool focused) mutable {
          if (input_generation.Get() != generation) return;
          try {
            if (focused) {
              (void)state->Activate(layout_document, path);
              revision = revision.Get() + 1;
            } else if (state->IsActive(layout_document, path)) {
              const auto epoch = state->focus_epoch;
              // Defer closure so focus can first enter another backing field.
              tasks.Post([=]() mutable {
                if (state->focus_epoch != epoch || !state->session.EditingBlock() || state->session.EditingValue().composition) return;
                try { state->EndEdit(); ++state->focus_epoch; revision = revision.Get() + 1; }
                catch (const std::exception& error) { report(error); }
              });
            }
          } catch (const std::exception& error) { report(error); }
        }).On<ViewEvents::KeyIntercept>([=](const KeyEvent& event) mutable {
          if (event.type != KeyEventType::Down || event.key != Key::Enter || (!table_cell && !event.modifiers.control) ||
              state->session.EditingValue().composition) return false;
          try {
            state->EndEdit();
            ++state->focus_epoch;
            input_generation = generation + 1;
            revision = revision.Get() + 1;
          }
          catch (const std::exception& error) { report(error); }
          return true;
        }).With(Opacity(active || projection ? 1.0F : 0.0F)).Key("markdown-block-input");
    if (projection) {
      input = std::move(input)
          .TextStyles(detail::BuildEditableStyles(*projection, base, theme, render_options.link_policy))
          .With(FontSize(base.font.Size()));
      auto field_style = TextFieldStyle::Default();
      field_style.text_style = base;
      field_style.padding = {};
      field_style.standard.minimum_height = 0;
      field_style.border_width = field_style.focused_border_width = 0;
      field_style.show_label = false;
      field_style.caret = theme.link;
      field_style.composition = theme.link;
      ThemeDefinition field_theme;
      field_theme.Set(field_style);
      return Theme(std::move(field_theme), Scope([input = View(std::move(input))] { return input; }).Key(generation));
    }
    // Suppress link hit targets so the backing source field receives the click.
    const auto renderer = Renderer(render_options, theme, events, nullptr, false);
    View preview = table_cell ? renderer.RenderInline(node, base, alignment) : renderer.RenderBlock(node);
    if (active) {
      if (live_preview) {
        auto fragment_options = state->parse_options;
        fragment_options.source_ranges = false;
        if (state->fragment_text != local.text || state->fragment == MarkdownDocument{}) {
          state->fragment = ParseMarkdown(local.text, fragment_options);
          state->fragment_text = local.text;
        }
        preview = renderer.Blocks(state->fragment.Root(), theme.block_spacing).Key("markdown-block-preview");
      } else preview = Column(std::vector<View>{});
    }
    return EditableBlockLayout{Scope([input = View(std::move(input))] { return input; }).Key(generation), std::move(preview)}
        .LayoutValue<EditingPresentation>(active).With(Spacing(active && live_preview ? 8.0F : 0.0F));
  });
}

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

std::vector<TextStyleRange> BuildEditableStyles(const MarkdownRichText& rich, const TextStyle& base,
                                                const MarkdownStyle& theme, const MarkdownLinkPolicy& policy) {
  std::vector<TextStyleRange> result;
  for (const auto& run : rich.Runs()) {
    TextSpanStyle style{.font = base.font, .foreground = base.foreground};
    for (const auto& mark : run.marks) {
      if (mark.kind == NodeKind::Link && !AllowsLinkDestination(mark.destination, policy)) continue;
      ApplyInlineStyle(mark.kind, theme, style);
    }
    result.push_back({run.range, std::move(style)});
  }
  return result;
}

std::vector<std::vector<std::size_t>> DocumentEditablePaths(const MarkdownDocument& document, std::string_view source,
                                                          std::size_t block) {
  const auto& node = document.Root().children.at(block);
  NodePath root{block};
  EditablePaths paths;
  (void)CollectEditablePaths(document, source, true, node, root, paths);
  std::vector<NodePath> ordered;
  for (auto& [leaf, path] : paths) {
    if (MarkdownRichText::Supports(*leaf)) ordered.push_back(std::move(path));
  }
  std::ranges::sort(ordered);
  return ordered;
}

View RenderEditableBlock(const MarkdownNode& node, const MarkdownStyle& style,
                         std::function<std::optional<View>(const MarkdownNode&, TextStyle, TextAlign)> editor) {
  const MarkdownOptions options{.selectable = false};
  return Renderer(options, style, {}, nullptr, false, std::move(editor)).RenderBlock(node);
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

View MarkdownEditor(TextEditingValue value, MarkdownEditorOptions options) {
  // Reuse TextField's UTF-8/scalar validation before mounting anything.
  const AttributedText text(value.text);
  (void)text.TextInRange({0, value.selection.anchor});
  (void)text.TextInRange({0, value.selection.active});
  if (value.composition) (void)text.TextInRange(*value.composition);
  if (value.selection.affinity != TextAffinity::Upstream && value.selection.affinity != TextAffinity::Downstream)
    throw std::invalid_argument("Invalid Markdown editor selection affinity");
  if (options.markdown_options.style) ValidateStyle(*options.markdown_options.style);
  const auto& virtualization = options.virtualization;
  if (!std::isfinite(virtualization.estimated_block_extent) || virtualization.estimated_block_extent <= 0 ||
      !std::isfinite(virtualization.cache_extent) || virtualization.cache_extent < 0)
    throw std::invalid_argument("Invalid Markdown editor virtualization extents");
  return Scope([value = std::move(value), options = std::move(options)]() -> View {
    const auto& virtualization = options.virtualization;
    auto holder = UseState(std::shared_ptr<BlockEditorState>{});
    if (!holder.Get() || holder.Get()->parse_options != options.parse_options || holder.Get()->rich_text != options.rich_text) {
      if (holder.Get() && holder.Get()->session.EditingValue().composition)
        throw std::logic_error("Finish IME composition before changing Markdown editor parsing options");
      holder = std::make_shared<BlockEditorState>(value, options.parse_options, options.rich_text);
    }
    const auto state = holder.Get();
    auto revision = UseState(std::uint64_t{0});
    (void)revision.Get();
    const auto events = UseEvents();
    const auto tasks = UseTaskScope();
    if (state->session.Source() != value.text) {
      state->session.UpdateSource(value.text);
      state->rich.reset();
      state->retained_row.reset();
      ++state->focus_epoch;
    }
    auto theme = options.markdown_options.style ? *options.markdown_options.style : MarkdownStyle::FromTheme(UseTheme());
    ValidateStyle(theme);
    auto render_options = options.markdown_options;
    render_options.selectable = false;
    const auto report = [events](const std::exception& error) {
      const std::string message = error.what();
      events.Emit<MarkdownEditorEvents::Error>(message);
    };
    if (state->empty_mode || EmptyForEditing(state->session.Document())) {
      state->empty_mode = true;
      View input = TextField(value).LineLimits(TextFieldLineLimits::MultiLine(1)).VerticalAlign(TextVerticalAlign::Top)
          .OnChanged([=](const TextEditingValue& next) mutable {
            try {
              state->session.UpdateSource(next.text);
              events.Emit<MarkdownEditorEvents::Changed>(next);
              revision = revision.Get() + 1;
            } catch (const std::exception& error) { report(error); }
          }).On<ViewEvents::FocusChanged>([=](bool focused) mutable {
            if (!focused && !EmptyForEditing(state->session.Document())) {
              tasks.Post([=]() mutable { state->empty_mode = false; revision = revision.Get() + 1; });
            }
          }).Key("markdown-empty-input");
      return ScrollView(Column{
        std::move(input),
        options.live_preview ? Renderer(render_options, theme, events, nullptr, false)
            .Blocks(state->session.Document().Root(), theme.block_spacing).Key("markdown-block-preview") :
            View(Column(std::vector<View>{})),
      }.With(Spacing(8.0F), CrossAlign(CrossAxisAlignment::Stretch)));
    }
    const auto layout_document = state->session.LayoutDocument();
    if (!state->layout.snapshot || state->layout.snapshot->document != layout_document)
      state->layout.snapshot = std::make_shared<const VirtualSnapshot>(layout_document, state->layout.snapshot,
          state->layout.next_id, std::exchange(state->retained_row, std::nullopt));
    const auto snapshot = state->layout.snapshot;
    const EditorRenderContext editor{state, layout_document, render_options, theme, events, tasks, revision, options.live_preview};
    auto list = VirtualList(snapshot->blocks.size(), [=](std::size_t index) -> View {
      const auto block = snapshot->blocks[index];
      return Scope([=]() -> View {
        (void)revision.Get();
        const auto field = [&](const NodePath& path, TextStyle base, TextAlign alignment) {
          return EditableField(editor, block, path, std::move(base), alignment);
        };
        if (state->session.LayoutDocument() != layout_document)
          return Renderer(render_options, theme, events).RenderBlock(block->node);
        NodePath path{index};
        EditablePaths paths;
        const bool complete = CollectEditablePaths(layout_document, state->session.LayoutSource(), options.rich_text,
            block->node, path, paths);
        if ((!options.rich_text || !complete || paths.empty()) && layout_document.TopLevelSourceRange(index))
          return field(path, theme.body, TextAlign::Leading);
        return Renderer(render_options, theme, events, nullptr, true,
            [&](const MarkdownNode& node, TextStyle base, TextAlign alignment) -> std::optional<View> {
              const auto found = paths.find(&node);
              if (found == paths.end()) return std::nullopt;
              return field(found->second, std::move(base), alignment);
            }).RenderBlock(block->node);
      }).With(Frame{.min_height = 0.001F}, Padding(EdgeInsets{0, 0, index + 1 < snapshot->blocks.size() ?
          BlockGap(block->node, snapshot->blocks[index + 1]->node, theme.block_spacing) : 0, 0})).Key(block->id);
    }).EstimatedItemExtent(virtualization.estimated_block_extent).CacheExtent(virtualization.cache_extent);
    if (virtualization.scroll_controller) list = std::move(list).Controller(*virtualization.scroll_controller);
    return View(std::move(list));
  });
}

} // namespace huxerui::markdown
