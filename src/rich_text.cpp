#include <huxerui/markdown/editing.h>
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace huxerui::markdown {
namespace {
std::vector<std::string> Scalars(std::string_view text) {
  (void)AttributedText(std::string(text));
  std::vector<std::string> result;
  for (std::size_t i = 0; i < text.size();) {
    const auto c = static_cast<unsigned char>(text[i]);
    const auto size = c < 0x80 ? 1U : c < 0xE0 ? 2U : c < 0xF0 ? 3U : 4U;
    result.emplace_back(text.substr(i, size));
    i += size;
  }
  return result;
}
bool InlineSupported(const MarkdownNode& node) noexcept {
  switch (node.kind) {
  case NodeKind::Paragraph:
  case NodeKind::Heading:
  case NodeKind::Text:
  case NodeKind::Emphasis:
  case NodeKind::Strong:
  case NodeKind::Strikethrough:
  case NodeKind::Underline:
  case NodeKind::Highlight:
  case NodeKind::Insertion:
  case NodeKind::Code:
  case NodeKind::Link:
  case NodeKind::WikiLink:
    break;
  default:
    return false;
  }
  return std::all_of(node.children.begin(), node.children.end(), InlineSupported);
}
std::string Escape(std::string_view text) {
  std::string result;
  for (const char c : text) {
    if (c == '&') {
      result += "&amp;";
      continue;
    }
    if (std::string_view("\\`*_{}[]<>()#+-.!|~=$\"").find(c) != std::string_view::npos)
      result += '\\';
    result += c;
  }
  return result;
}
std::string Entity(std::string_view scalar) {
  std::uint32_t cp = 0;
  const auto first = static_cast<unsigned char>(scalar.front());
  cp = scalar.size() == 1 ? first : scalar.size() == 2 ? first & 31 : scalar.size() == 3 ? first & 15 : first & 7;
  for (std::size_t i = 1; i < scalar.size(); ++i)
    cp = (cp << 6) | (static_cast<unsigned char>(scalar[i]) & 63);
  return "&#" + std::to_string(cp) + ";";
}
} // namespace

bool MarkdownRichText::Supports(const MarkdownNode& node) noexcept {
  return (node.kind == NodeKind::Paragraph || node.kind == NodeKind::Heading) && InlineSupported(node);
}
MarkdownRichText::MarkdownRichText(const MarkdownNode& node, ParseOptions options)
    : kind_(node.kind), heading_level_(node.heading_level), options_(options) {
  if (!Supports(node))
    throw std::invalid_argument("Markdown block requires source editing");
  options_.source_ranges = false;
  Append(node, {});
  std::string text;
  for (const auto& character : characters_)
    text += character.text;
  value_ = TextEditingValue::FromText(std::move(text));
}
void MarkdownRichText::Append(const MarkdownNode& node, std::vector<MarkdownTextMark> marks) {
  if (node.kind != NodeKind::Text && node.kind != NodeKind::Paragraph && node.kind != NodeKind::Heading)
    marks.push_back({node.kind, node.destination, node.title});
  for (auto& scalar : Scalars(node.text))
    characters_.push_back({std::move(scalar), marks});
  for (const auto& child : node.children)
    Append(child, marks);
}
std::vector<MarkdownRichTextRun> MarkdownRichText::Runs() const {
  std::vector<MarkdownRichTextRun> result;
  TextOffset offset = 0;
  for (const auto& character : characters_) {
    const auto length = character.text.size() == 4 ? 2 : 1;
    if (!result.empty() && result.back().marks == character.marks)
      result.back().range.end += length;
    else
      result.push_back({{offset, offset + length}, character.marks});
    offset += length;
  }
  return result;
}
TextEditingValue MarkdownRichText::Serialize(bool entities) const {
  std::string source;
  std::vector<std::size_t> starts(characters_.size()), ends(characters_.size());
  if (kind_ == NodeKind::Heading)
    source = std::string(std::clamp(heading_level_, 1U, 6U), '#') + " ";
  std::function<void(std::size_t, std::size_t, std::size_t)> emit;
  emit = [&](std::size_t first, std::size_t last, std::size_t depth) {
    for (auto i = first; i < last;) {
      if (characters_[i].marks.size() <= depth) {
        const auto& text = characters_[i].text;
        starts[i] = source.size();
        if (text == "\n")
          source += kind_ == NodeKind::Heading ? "\n" + std::string(heading_level_, '#') + " " : "  \n";
        else if (text == "\r")
          source += "&#13;";
        else if (entities || text == "\t" || (text == " " && (i == 0 || characters_[i - 1].text == "\n")))
          source += Entity(text);
        else
          source += Escape(text);
        ends[i] = source.size();
        ++i;
        continue;
      }
      const auto& mark = characters_[i].marks[depth];
      auto end = i + 1;
      while (end < last && characters_[end].marks.size() > depth &&
             std::equal(characters_[i].marks.begin(), characters_[i].marks.begin() + depth + 1,
                        characters_[end].marks.begin()))
        ++end;
      std::string open, close;
      switch (mark.kind) {
      case NodeKind::Strong:
        open = close = "**";
        break;
      case NodeKind::Emphasis:
        open = close = "*";
        break;
      case NodeKind::Strikethrough:
        open = close = "~~";
        break;
      case NodeKind::Underline:
        open = close = "_";
        break;
      case NodeKind::Highlight:
        open = close = "==";
        break;
      case NodeKind::Insertion:
        open = close = "++";
        break;
      case NodeKind::Link:
        open = "[";
        close = "](<" + Escape(mark.destination) + ">" + (mark.title.empty() ? "" : " \"" + Escape(mark.title) + "\"") +
                ")";
        break;
      case NodeKind::WikiLink:
        open = "[[" + Escape(mark.destination) + "|";
        close = "]]";
        break;
      case NodeKind::Code: {
        std::string body;
        for (auto n = i; n < end; ++n)
          body += characters_[n].text;
        if (body.find_first_of("\r\n") != std::string::npos)
          throw std::invalid_argument("Inline code cannot contain a line break");
        std::size_t longest = 0, run = 0;
        for (const char c : body) {
          run = c == '`' ? run + 1 : 0;
          longest = std::max(longest, run);
        }
        open = close = std::string(longest + 1, '`');
        if (!body.empty() && !std::all_of(body.begin(), body.end(), [](char c) { return c == ' '; }) &&
            (body.front() == '`' || body.back() == '`' || body.front() == ' ' || body.back() == ' ')) {
          open += ' ';
          close.insert(close.begin(), ' ');
        }
        source += open;
        for (auto n = i; n < end; ++n) {
          starts[n] = source.size();
          source += characters_[n].text;
          ends[n] = source.size();
        }
        source += close;
        i = end;
        continue;
      }
      default:
        throw std::invalid_argument("Unsupported Markdown rich text mark");
      }
      source += open;
      emit(i, end, depth + 1);
      source += close;
      i = end;
    }
  };
  emit(0, characters_.size(), 0);
  const auto map = [&](TextOffset offset, TextAffinity affinity) {
    TextOffset current = 0;
    for (std::size_t i = 0; i < characters_.size(); ++i) {
      if (current == offset)
        return affinity == TextAffinity::Upstream && i ? ends[i - 1] : starts[i];
      current += characters_[i].text.size() == 4 ? 2 : 1;
    }
    return characters_.empty() ? source.size() : ends.back();
  };
  const auto convert = [&](TextOffset offset, TextAffinity affinity) {
    return AttributedText(source.substr(0, map(offset, affinity))).Length();
  };
  auto result = TextEditingValue::FromText(source);
  result.selection = {convert(value_.selection.anchor, value_.selection.affinity),
                      convert(value_.selection.active, value_.selection.affinity), value_.selection.affinity};
  if (value_.composition) {
    const auto start = convert(value_.composition->start, TextAffinity::Downstream);
    const auto end =
        value_.composition->IsCollapsed() ? start : convert(value_.composition->end, TextAffinity::Upstream);
    result.composition = TextRange{start, std::max(start, end)};
  }
  return result;
}
TextEditingValue MarkdownRichText::SourceValue() const {
  for (const bool entities : {false, true}) {
    auto source = Serialize(entities);
    const auto parsed = ParseMarkdown(source.text, options_);
    if (value_.text.empty() && parsed.PlainText().empty())
      return source;
    if (parsed.Root().children.size() != 1 || !Supports(parsed.Root().children.front()))
      continue;
    MarkdownRichText restored(parsed.Root().children.front(), options_);
    if (restored.characters_ == characters_)
      return source;
  }
  throw std::invalid_argument("This formatted edit cannot be represented faithfully in Markdown; use source mode");
}
void MarkdownRichText::Update(const TextEditingValue& value) {
  const AttributedText text(value.text);
  (void)text.TextInRange({0, value.selection.anchor});
  (void)text.TextInRange({0, value.selection.active});
  if (value.composition)
    (void)text.TextInRange(*value.composition);
  if (value.selection.affinity != TextAffinity::Upstream && value.selection.affinity != TextAffinity::Downstream)
    throw std::invalid_argument("Invalid Markdown rich text affinity");
  auto next = *this;
  if (!value_.composition && value.composition) {
    next.composition_baseline_ = characters_;
    next.composition_text_ = value_.text;
  }
  if (value_.composition && !value.composition && value.text == composition_text_ && composition_baseline_)
    next.characters_ = *composition_baseline_;
  else if (value.text != value_.text) {
    bool restored = false;
    if (!value_.composition && !value.composition) {
      for (auto old = history_.rbegin(); old != history_.rend(); ++old) {
        if (old->text != value.text)
          continue;
        const auto document = ParseMarkdown(old->source, options_);
        if (value.text.empty())
          next.characters_.clear();
        else
          next.characters_ = MarkdownRichText(document.Root().children.at(0), options_).characters_;
        restored = true;
        break;
      }
    }
    if (!restored) {
      const auto scalars = Scalars(value.text);
      std::size_t prefix = 0, suffix = 0;
      while (prefix < scalars.size() && prefix < characters_.size() && scalars[prefix] == characters_[prefix].text)
        ++prefix;
      while (suffix < scalars.size() - prefix && suffix < characters_.size() - prefix &&
             scalars[scalars.size() - suffix - 1] == characters_[characters_.size() - suffix - 1].text)
        ++suffix;
      const auto scalar_at = [&](TextOffset offset) {
        TextOffset units = 0;
        std::size_t index = 0;
        while (index < characters_.size() && units < offset)
          units += characters_[index++].text.size() == 4 ? 2 : 1;
        return index;
      };
      const auto prefer = [&](TextRange target) {
        if (!target.IsValid())
          return;
        try {
          (void)AttributedText(value_.text).TextInRange(target);
        } catch (const std::invalid_argument&) {
          return;
        }
        const auto start = scalar_at(target.start), end = scalar_at(target.end);
        const auto tail = characters_.size() - end;
        if (start + tail > scalars.size())
          return;
        for (std::size_t i = 0; i < start; ++i)
          if (characters_[i].text != scalars[i])
            return;
        for (std::size_t i = 0; i < tail; ++i)
          if (characters_[end + i].text != scalars[scalars.size() - tail + i])
            return;
        prefix = start;
        suffix = tail;
      };
      if (!value_.composition && value_.selection.IsCollapsed() && value.selection.IsCollapsed() &&
          AttributedText(value.text).Length() < AttributedText(value_.text).Length()) {
        const auto removed = AttributedText(value_.text).Length() - AttributedText(value.text).Length();
        prefer({value.selection.active, value.selection.active + removed});
      } else
        prefer(value_.composition.value_or(value_.selection.Range()));
      std::vector<MarkdownTextMark> marks;
      if (prefix < characters_.size() - suffix)
        marks = characters_[prefix].marks;
      else if (prefix)
        marks = characters_[prefix - 1].marks;
      else if (!characters_.empty())
        marks = characters_.front().marks;
      std::vector<Character> changed(characters_.begin(), characters_.begin() + prefix);
      for (auto i = prefix; i < scalars.size() - suffix; ++i)
        changed.push_back({scalars[i], marks});
      changed.insert(changed.end(), characters_.end() - suffix, characters_.end());
      next.characters_ = std::move(changed);
    }
  }
  next.value_ = value;
  if (!value.composition) {
    next.composition_baseline_.reset();
    next.composition_text_.clear();
  }
  if (value.text != value_.text) {
    (void)next.SourceValue();
    // Native TextField undo carries display text, not semantic marks. Retain a
    // bounded local cache so revisiting recent text restores its formatting too.
    auto source = SourceValue().text;
    next.history_bytes_ += value_.text.size() + source.size();
    next.history_.push_back({value_.text, std::move(source)});
    while (!next.history_.empty() && (next.history_.size() > 32 || next.history_bytes_ > 2 * 1024 * 1024)) {
      next.history_bytes_ -= next.history_.front().text.size() + next.history_.front().source.size();
      next.history_.erase(next.history_.begin());
    }
  }
  *this = std::move(next);
}
} // namespace huxerui::markdown
