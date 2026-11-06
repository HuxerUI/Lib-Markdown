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
