#include <huxerui/markdown/editing.h>
#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace huxerui::markdown {
namespace {
TextEditingValue Edited(std::string text, std::size_t anchor, std::size_t active, TextAffinity affinity) {
  const auto a = AttributedText(text.substr(0, anchor)).Length();
  const auto b = AttributedText(text.substr(0, active)).Length();
  auto result = TextEditingValue::FromText(std::move(text));
  result.selection = {a, b, affinity};
  return result;
}
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
} // namespace huxerui::markdown
