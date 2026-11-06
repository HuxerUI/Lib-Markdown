#include <huxerui/markdown/editing.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>

#include "editing_internal.h"

namespace huxerui::markdown {
namespace {
TextEditingValue Edited(std::string text, std::size_t anchor, std::size_t active, TextAffinity affinity) {
  const auto a = AttributedText(text.substr(0, anchor)).Length();
  const auto b = AttributedText(text.substr(0, active)).Length();
  auto result = TextEditingValue::FromText(std::move(text));
  result.selection = {a, b, affinity};
  return result;
}

ParseOptions EditingOptions(ParseOptions options) { options.source_ranges = true; return options; }

void ValidateEditingValue(const TextEditingValue& value) {
  const AttributedText text(value.text);
  (void)text.TextInRange({0, value.selection.anchor});
  (void)text.TextInRange({0, value.selection.active});
  if (value.selection.affinity != TextAffinity::Upstream && value.selection.affinity != TextAffinity::Downstream)
    throw std::invalid_argument("Invalid Markdown selection affinity");
  if (value.composition) (void)text.TextInRange(*value.composition);
}

void ReplaceNestedContent(MarkdownNode& root, std::span<const std::size_t> path,
                          std::string_view source, ParseOptions options) {
  if (source.find_first_of("\r\n") != std::string_view::npos)
    throw std::invalid_argument("Nested Markdown content requires a single line");
  auto* parent = &root;
  for (const auto index : path.first(path.size() - 1)) parent = &parent->children[index];
  auto& leaf = parent->children[path.back()];
  if (detail::IsTableCell(leaf.kind)) {
    auto cell = detail::ParseTableCell(source, options);
    if (!cell) throw std::invalid_argument("Markdown edit must stay inside the table cell");
    leaf.children = std::move(cell->children);
    return;
  }
  const auto local_source = leaf.kind == NodeKind::Heading
      ? std::string(leaf.heading_level, '#') + " " + std::string(source) : std::string(source);
  const auto parsed = ParseMarkdown(local_source, options);
  const auto& children = parsed.Root().children;
  if (leaf.kind == NodeKind::Paragraph && children.empty()) {
    parent->children.erase(parent->children.begin() + path.back());
    return;
  }
  if (children.size() != 1 || children.front().kind != leaf.kind)
    throw std::invalid_argument("Markdown edit must stay inside the current text block");
  leaf = children.front();
}

void AppendMarkedText(MarkdownNode& block, std::string text, std::span<const MarkdownTextMark> marks) {
  if (text.empty()) return;
  MarkdownNode content;
  content.text = std::move(text);
  for (auto index = marks.size(); index > 0; --index) {
    const auto& mark = marks[index - 1];
    MarkdownNode wrapper;
    wrapper.kind = mark.kind;
    wrapper.destination = mark.destination;
    wrapper.title = mark.title;
    wrapper.children.push_back(std::move(content));
    content = std::move(wrapper);
  }
  block.children.push_back(std::move(content));
}

void AppendRichSlice(MarkdownNode& block, const AttributedText& text,
                     std::span<const MarkdownRichTextRun> runs, TextRange range) {
  for (const auto& run : runs) {
    const TextRange intersection{std::max(run.range.start, range.start), std::min(run.range.end, range.end)};
    if (intersection.start < intersection.end)
      AppendMarkedText(block, text.TextInRange(intersection), run.marks);
  }
}

std::span<const MarkdownTextMark> MarksAt(std::span<const MarkdownRichTextRun> runs, TextOffset offset) {
  for (const auto& run : runs) {
    if (run.range.start <= offset && offset < run.range.end) return run.marks;
  }
  return {};
}

std::string_view LineEnding(std::string_view source, std::size_t start) {
  auto at = source.find_first_of("\r\n", start);
  if (at == std::string_view::npos) at = source.find_first_of("\r\n");
  if (at == std::string_view::npos || source[at] == '\n') return "\n";
  return source.substr(at).starts_with("\r\n") ? "\r\n" : "\r";
}
}

std::optional<MarkdownNode> detail::ParseTableCell(std::string_view source, ParseOptions options) {
  if (source.find_first_of("\r\n") != std::string_view::npos) return std::nullopt;
  options.source_ranges = true;
  // The wrapper is verification syntax and does not count toward document input limits.
  auto wrapped = "| " + std::string(source) + " |\n| --- |";
  options.max_input_bytes = std::max(options.max_input_bytes, wrapped.size());
  const auto table = ParseMarkdown(wrapped, options);
  const auto& blocks = table.Root().children;
  if (blocks.size() != 1 || blocks.front().kind != NodeKind::Table) return std::nullopt;
  const auto& cells = blocks.front().children.front().children.front().children;
  if (cells.size() != 1) return std::nullopt;
  // MD4C ignores surplus cells. Verify that it consumed the entire fragment
  // as one cell before accepting a write that could hide text after a pipe.
  const auto first = source.find_first_not_of(" \t");
  if (first != std::string_view::npos) {
    const auto range = table.SourceRange(std::array<std::size_t, 4>{0, 0, 0, 0});
    if (!range || range->start_byte != first + 2 || range->end_byte != source.find_last_not_of(" \t") + 3)
      return std::nullopt;
  }
  return cells.front();
}

detail::MarkdownDocumentEditSession::MarkdownDocumentEditSession(std::string_view source, ParseOptions options)
    : options_(options), parser_(EditingOptions(options)) {
  options_.source_ranges = false;
  (void)parser_.Update(source);
  const auto& nodes = Document().Root().children;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].kind != NodeKind::BlankLines) blocks_.push_back({i, Document().TopLevelSourceRange(i)});
  }
  if (blocks_.empty() && source.find_first_not_of(" \t\r\n") == std::string_view::npos) {
    const auto line_end = source.find_last_of("\r\n");
    const auto line_start = line_end == std::string_view::npos ? 0 : line_end + 1;
    blocks_.push_back({{}, MarkdownSourceRange{line_start, source.size()}});
  }
}

detail::MarkdownDocumentEditSession::MarkdownDocumentEditSession(const MarkdownDocumentEditSession& other)
    : options_(other.options_), parser_(other.parser_.Fork()), blocks_(other.blocks_) {}

const MarkdownNode& detail::MarkdownDocumentEditSession::Block(std::size_t index) const {
  const auto syntax_index = blocks_.at(index).syntax_index;
  static const MarkdownNode empty{.kind = NodeKind::Paragraph};
  return syntax_index ? Document().Root().children[*syntax_index] : empty;
}

std::optional<MarkdownSourceRange> detail::MarkdownDocumentEditSession::SourceRange(std::size_t index) const {
  return blocks_.at(index).source;
}

detail::MarkdownBlockPosition detail::MarkdownDocumentEditSession::Replace(
    MarkdownBlockPosition first, MarkdownBlockPosition last, std::string_view replacement) {
  if (first.block > last.block || last.block >= blocks_.size() ||
      (first.block == last.block && first.offset > last.offset)) {
    throw std::invalid_argument("Markdown document edit range is invalid");
  }
  if (replacement.find('\r') != std::string_view::npos)
    throw std::invalid_argument("Markdown paragraph separators use LF coordinates");
  const auto start_range = SourceRange(first.block);
  const auto end_range = SourceRange(last.block);
  if (!start_range || !end_range) {
    throw std::invalid_argument("Markdown document edit requires verified source ranges");
  }
  auto consumed = start_range->start_byte;
  for (auto index = first.block; index <= last.block; ++index) {
    const auto range = SourceRange(index);
    if (!MarkdownRichText::Supports(Block(index)) || !range || range->start_byte < consumed ||
        Source().substr(consumed, range->start_byte - consumed).find_first_not_of(" \t\r\n") !=
            std::string_view::npos) {
      throw std::invalid_argument("Markdown selection crosses content that requires source editing");
    }
    consumed = range->end_byte;
  }

  const auto& first_node = Block(first.block);
  const MarkdownRichText head(first_node, options_);
  const MarkdownRichText tail(Block(last.block), options_);
  const AttributedText head_text(head.Value().text);
  const AttributedText tail_text(tail.Value().text);
  (void)head_text.TextInRange({first.offset, first.offset});
  (void)tail_text.TextInRange({last.offset, last.offset});
  if (first.block == last.block && head_text.TextInRange({first.offset, last.offset}) == replacement) return last;

  const auto head_runs = head.Runs();
  const auto tail_runs = tail.Runs();
  const bool insertion = first == last;
  const auto mark_offset = first.offset > 0 && (insertion || first.offset == head_text.Length())
      ? first.offset - 1 : first.offset;
  const auto marks = MarksAt(head_runs, mark_offset);
  const auto eol = LineEnding(Source(), start_range->start_byte);
  struct FormattedBlock {
    MarkdownSourceRange range;
    std::optional<MarkdownNode> node;
  };
  std::vector<FormattedBlock> formatted;
  std::string source;
  TextOffset caret_offset = first.offset;
  while (true) {
    const auto separator = replacement.find('\n');
    const auto piece = replacement.substr(0, separator);
    MarkdownNode block;
    block.kind = NodeKind::Paragraph;
    if (formatted.empty()) {
      block.kind = first_node.kind;
      block.heading_level = first_node.heading_level;
      AppendRichSlice(block, head_text, head_runs, {0, first.offset});
    } else {
      source.append(eol);
      source.append(eol);
      caret_offset = 0;
    }
    AppendMarkedText(block, std::string(piece), marks);
    if (separator == std::string_view::npos)
      AppendRichSlice(block, tail_text, tail_runs, {last.offset, tail_text.Length()});
    const auto serialized = MarkdownRichText(block, options_).SourceValue().text;
    const auto start_byte = start_range->start_byte + source.size();
    source += serialized;
    const MarkdownSourceRange range{start_byte, start_byte + serialized.size()};
    if (serialized.empty()) {
      formatted.push_back({range, {}});
    } else {
      const auto fragment = ParseMarkdown(serialized, options_);
      const auto& nodes = fragment.Root().children;
      if (nodes.size() != 1 || nodes.front().kind != block.kind)
        throw std::invalid_argument("Markdown edit could not preserve the text block kind");
      formatted.push_back({range, nodes.front()});
    }
    if (separator == std::string_view::npos) {
      caret_offset += AttributedText(std::string(piece)).Length();
      break;
    }
    replacement.remove_prefix(separator + 1);
  }

  const MarkdownEdit edit{start_range->start_byte, end_range->end_byte - start_range->start_byte,
                          std::move(source)};
  // The splice must preserve all surrounding syntax. BlankLines can be
  // coalesced by the parser, so empty editor rows keep their own byte anchors.
  std::vector<BlockLocation> locations;
  const auto original = Document();
  const auto validate = [&](const MarkdownDocument& next) {
    const std::span<const MarkdownNode> updated = next.Root().children;
    std::size_t index = 0;
    const auto skip_blank_lines = [&] {
      while (index < updated.size() && updated[index].kind == NodeKind::BlankLines) ++index;
    };
    const auto add_node = [&](const MarkdownNode& expected) {
      skip_blank_lines();
      if (index == updated.size() || updated[index] != expected)
        throw std::invalid_argument("Markdown document edit would change surrounding structure");
      locations.push_back({index, next.TopLevelSourceRange(index)});
      ++index;
    };
    const auto retain = [&](const BlockLocation& location, bool after) {
      if (location.syntax_index) {
        add_node(original.Root().children[*location.syntax_index]);
      } else {
        auto at = location.source->start_byte;
        if (after) at = at - edit.removed_bytes + edit.inserted_text.size();
        locations.push_back({{}, MarkdownSourceRange{at, at}});
      }
    };
    for (std::size_t i = 0; i < first.block; ++i) retain(blocks_[i], false);
    for (const auto& part : formatted) {
      if (!part.node) {
        locations.push_back({{}, part.range});
        continue;
      }
      add_node(*part.node);
      const auto verified = locations.back().source;
      if (!verified || verified->start_byte < part.range.start_byte || verified->end_byte > part.range.end_byte)
        throw std::invalid_argument("Markdown replacement has no verified source range");
    }
    for (std::size_t i = last.block + 1; i < blocks_.size(); ++i) retain(blocks_[i], true);
    skip_blank_lines();
    if (index != updated.size()) throw std::invalid_argument("Markdown document edit would introduce an extra block");
  };
  if (Source().substr(edit.start_byte, edit.removed_bytes) == edit.inserted_text) validate(original);
  else (void)parser_.ApplyEdit(edit, validate);
  blocks_ = std::move(locations);
  return {first.block + formatted.size() - 1, caret_offset};
}

TextEditingValue ApplyMarkdownCommand(const TextEditingValue& value, MarkdownEditCommand command) {
  const AttributedText original(value.text);
  if (value.selection.affinity != TextAffinity::Upstream && value.selection.affinity != TextAffinity::Downstream)
    throw std::invalid_argument("Invalid Markdown selection affinity");
  auto anchor = original.TextInRange({0, value.selection.anchor}).size();
  auto active = original.TextInRange({0, value.selection.active}).size();
  if (value.composition) throw std::logic_error("Finish IME composition before Markdown formatting");
  const auto first = std::min(anchor, active), last = std::max(anchor, active);
  const auto selected = value.text.substr(first, last - first);
  std::string prefix, suffix;
  bool block = false;
  switch (command) {
  case MarkdownEditCommand::Bold: prefix = suffix = "**"; break;
  case MarkdownEditCommand::Italic: prefix = suffix = "*"; break;
  case MarkdownEditCommand::InlineCode: {
    if (selected.find_first_of("\r\n") != std::string::npos)
      throw std::invalid_argument("Inline code requires a single-line selection");
    std::size_t longest = 0, run = 0;
    for (const char c : selected) { run = c == '`' ? run + 1 : 0; longest = std::max(longest, run); }
    prefix = suffix = std::string(longest + 1, '`');
    if (!selected.empty() && !std::all_of(selected.begin(), selected.end(), [](char c) { return c == ' '; }) &&
        (selected.front() == '`' || selected.back() == '`' || selected.front() == ' ' || selected.back() == ' ')) {
      prefix += ' ';
      suffix.insert(suffix.begin(), ' ');
    }
    break;
  }
  case MarkdownEditCommand::Heading1: prefix = "# "; block = true; break;
  case MarkdownEditCommand::Heading2: prefix = "## "; block = true; break;
  case MarkdownEditCommand::BulletList: prefix = "- "; block = true; break;
  case MarkdownEditCommand::BlockQuote: prefix = "> "; block = true; break;
  default: throw std::invalid_argument("Invalid Markdown edit command");
  }
  auto text = value.text;
  if (!block) {
    text.replace(first, last - first, prefix + selected + suffix);
    const auto start = first + prefix.size(), end = start + selected.size();
    return Edited(std::move(text), anchor > active ? end : start, anchor > active ? start : end, value.selection.affinity);
  }

  std::vector<std::size_t> starts{0};
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
      starts.push_back(i + 1);
    } else if (text[i] == '\n') starts.push_back(i + 1);
  }
  const auto line_at = [&](std::size_t byte) {
    return static_cast<std::size_t>(std::upper_bound(starts.begin(), starts.end(), byte) - starts.begin() - 1);
  };
  const auto first_line = line_at(first), last_line = line_at(last > first ? last - 1 : last);
  for (auto line = last_line + 1; line-- > first_line;) {
    auto at = starts[line];
    std::size_t removed = 0;
    if (command == MarkdownEditCommand::Heading1 || command == MarkdownEditCommand::Heading2) {
      auto marker = at;
      while (marker < text.size() && marker - at < 3 && text[marker] == ' ') ++marker;
      auto end = marker;
      while (end < text.size() && text[end] == '#') ++end;
      if (end > marker && end - marker <= 6 &&
          (end == text.size() || text[end] == ' ' || text[end] == '\t' || text[end] == '\r' || text[end] == '\n')) {
        at = marker;
        while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) ++end;
        removed = end - marker;
      }
    } else if (std::string_view(text).substr(at).starts_with(prefix)) continue;
    const auto remap = [&](std::size_t offset) {
      if (offset >= at + removed) return offset - removed + prefix.size();
      return offset >= at ? at + prefix.size() : offset;
    };
    anchor = remap(anchor);
    active = remap(active);
    text.replace(at, removed, prefix);
  }
  return Edited(std::move(text), anchor, active, value.selection.affinity);
}

MarkdownBlockEditSession::MarkdownBlockEditSession(std::string_view source, ParseOptions options)
    : options_(EditingOptions(options)), parser_(options_) { UpdateSource(source); }
std::string_view MarkdownBlockEditSession::Source() const noexcept { return parser_.Source(); }
const MarkdownDocument& MarkdownBlockEditSession::Document() const noexcept { return parser_.Document(); }
const MarkdownDocument& MarkdownBlockEditSession::LayoutDocument() const noexcept { return layout_document_; }
std::string_view MarkdownBlockEditSession::LayoutSource() const noexcept { return layout_source_; }
std::optional<std::size_t> MarkdownBlockEditSession::EditingBlock() const noexcept {
  return path_.empty() ? std::nullopt : std::optional{path_.front()};
}
const TextEditingValue& MarkdownBlockEditSession::EditingValue() const noexcept { return value_; }
void MarkdownBlockEditSession::UpdateSource(std::string_view source) {
  if (source == Source()) return;
  if (!path_.empty() && value_.composition) throw std::logic_error("Finish IME composition before replacing Markdown source");
  std::string owned(source);
  const auto next = parser_.Update(owned);
  layout_source_.swap(owned);
  layout_document_ = next;
  path_.clear();
  value_ = {};
}
void MarkdownBlockEditSession::BeginEdit(std::size_t block) {
  BeginEdit(std::span(&block, 1));
}
void MarkdownBlockEditSession::BeginEdit(std::span<const std::size_t> path) {
  if (!path.empty() && std::ranges::equal(path_, path)) return;
  if (!path_.empty() && value_.composition) throw std::logic_error("Finish IME composition before switching Markdown blocks");
  const auto range = Document().SourceRange(path);
  if (!range) throw std::invalid_argument("Markdown block has no verified editable source range");
  std::vector<std::size_t> owned_path(path.begin(), path.end());
  auto value = TextEditingValue::FromText(std::string(Source().substr(range->start_byte, range->end_byte - range->start_byte)));
  std::string source(Source());
  layout_source_.swap(source);
  layout_document_ = Document();
  path_.swap(owned_path);
  range_ = *range;
  value_ = std::move(value);
}
void MarkdownBlockEditSession::UpdateEdit(const TextEditingValue& value) {
  if (path_.empty()) throw std::logic_error("No Markdown block is being edited");
  ValidateEditingValue(value);
  auto owned = value;
  const auto end = range_.start_byte + value.text.size();
  const MarkdownEdit edit{range_.start_byte, range_.end_byte - range_.start_byte, value.text};
  if (path_.size() > 1 && value.text != value_.text) {
    auto options = options_;
    options.source_ranges = false;
    auto expected = layout_document_.Root();
    ReplaceNestedContent(expected, path_, value.text, options);
    (void)parser_.ApplyEdit(edit, [&](const MarkdownDocument& next) {
      if (next.Root() != expected) throw std::invalid_argument("Markdown edit would change surrounding structure");
    });
  } else {
    (void)parser_.ApplyEdit(edit);
  }
  range_.end_byte = end;
  value_ = std::move(owned);
}
void MarkdownBlockEditSession::ApplyCommand(MarkdownEditCommand command) {
  if (path_.empty()) throw std::logic_error("No Markdown block is being edited");
  UpdateEdit(ApplyMarkdownCommand(value_, command));
}
void MarkdownBlockEditSession::EndEdit() {
  if (path_.empty()) return;
  if (value_.composition) throw std::logic_error("Finish IME composition before ending Markdown block editing");
  std::string source(Source());
  layout_source_.swap(source);
  layout_document_ = Document();
  path_.clear();
  value_ = {};
}
} // namespace huxerui::markdown
