#include "model.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace huxerui::rich_text::detail {
namespace {

Document InitialDocument(std::vector<AttributedText> values) {
  if (values.empty()) values.emplace_back();
  std::vector<Paragraph> paragraphs;
  paragraphs.reserve(values.size());
  for (auto& value : values) {
    if (value.PlainText().find('\n') != std::string::npos) {
      throw std::invalid_argument("Rich text paragraph must not contain a paragraph separator");
    }
    paragraphs.push_back({static_cast<TextBlockId>(paragraphs.size()) + 1, std::move(value)});
  }
  return Document(std::move(paragraphs));
}

void ValidateSelection(const Document& document, TextSelection selection) {
  static_cast<void>(document.Locate(selection.anchor));
  static_cast<void>(document.Locate(selection.active));
  if (selection.affinity != TextAffinity::Upstream && selection.affinity != TextAffinity::Downstream) {
    throw std::invalid_argument("Rich text selection affinity is invalid");
  }
}

TextSpanStyle InsertionStyle(const AttributedText& text, TextOffset offset, bool replacing) {
  if (offset > 0 && (!replacing || offset == text.Length())) --offset;
  for (const auto& range : text.StyleRanges()) {
    if (range.range.start <= offset && offset < range.range.end) return range.style;
  }
  return {};
}

struct ParagraphBuilder {
  std::string body;
  std::vector<TextStyleRange> styles;
  std::vector<TextLinkRange> links;
  TextOffset length = 0;

  void Append(const AttributedText& text, TextRange range) {
    if (range.IsCollapsed()) return;
    body += text.TextInRange(range);
    const auto copy_ranges = [&](auto source, auto& destination) {
      for (auto item : source) {
        const auto start = std::max(item.range.start, range.start);
        const auto end = std::min(item.range.end, range.end);
        if (start < end) {
          item.range = {length + (start - range.start), length + (end - range.start)};
          destination.push_back(std::move(item));
        }
      }
    };
    copy_ranges(text.StyleRanges(), styles);
    copy_ranges(text.LinkRanges(), links);
    length += range.Length();
  }

  AttributedText Build() && {
    return AttributedText::FromRanges(std::move(body), std::move(styles), std::move(links));
  }
};

Snapshot Replaced(const Snapshot& previous, TextRange target, const AttributedText& inserted,
                  const std::optional<TextSelection>& selection_after, TextBlockId& next_id) {
  const auto& original = previous.document;
  if (!target.IsValid()) throw std::invalid_argument("Rich text replacement range must be ordered");
  const auto first = original.Locate(target.start);
  const auto last = original.Locate(target.end);
  if (inserted.Length() > std::numeric_limits<TextOffset>::max() - (original.Length() - target.Length())) {
    throw std::length_error("Rich text replacement exceeds the supported length");
  }
  auto selection = selection_after.value_or(TextSelection{target.start + inserted.Length(),
                                                          target.start + inserted.Length()});
  if (original.TextInRange(target) == inserted.PlainText()) {
    ValidateSelection(original, selection);
    return {original, selection};
  }

  const auto old = original.Paragraphs();
  const auto prefix = old.first(first.index);
  std::vector<Paragraph> paragraphs(prefix.begin(), prefix.end());
  const auto style = InsertionStyle(old[first.index].text, first.offset, !target.IsCollapsed());
  ParagraphBuilder builder;
  builder.Append(old[first.index].text, {0, first.offset});
  auto id = old[first.index].id;
  std::string_view remaining = inserted.PlainText();
  while (true) {
    const auto separator = remaining.find('\n');
    const AttributedText part{std::string(remaining.substr(0, separator))};
    if (part.Length() != 0) {
      builder.Append(part.WithStyles({{{0, part.Length()}, style}}), {0, part.Length()});
    }
    if (separator == std::string_view::npos) break;
    paragraphs.push_back({id, std::move(builder).Build()});
    builder = {};
    if (next_id == std::numeric_limits<TextBlockId>::max()) {
      throw std::length_error("Rich text paragraph identifiers are exhausted");
    }
    id = next_id++;
    remaining.remove_prefix(separator + 1);
  }
  builder.Append(old[last.index].text, {last.offset, old[last.index].text.Length()});
  paragraphs.push_back({id, std::move(builder).Build()});
  const auto suffix = old.subspan(last.index + 1);
  paragraphs.insert(paragraphs.end(), suffix.begin(), suffix.end());
  Document document(std::move(paragraphs));
  ValidateSelection(document, selection);
  return {std::move(document), selection};
}

AttributedText WithWeight(const AttributedText& text, TextRange selection, FontWeight weight) {
  std::vector<TextStyleRange> styles;
  const auto format = [&](TextRange range, TextSpanStyle style) {
    const auto start = std::clamp(selection.start, range.start, range.end);
    const auto end = std::clamp(selection.end, range.start, range.end);
    if (range.start < start) styles.push_back({{range.start, start}, style});
    if (start < end) {
      auto formatted = style;
      formatted.font_weight = weight;
      styles.push_back({{start, end}, std::move(formatted)});
    }
    if (end < range.end) styles.push_back({{end, range.end}, std::move(style)});
  };
  TextOffset offset = 0;
  for (const auto& range : text.StyleRanges()) {
    format({offset, range.range.start}, {});
    format(range.range, range.style);
    offset = range.range.end;
  }
  format({offset, text.Length()}, {});
  return text.WithStyles(std::move(styles));
}

void RequireCommand(bool valid) {
  if (!valid) throw std::invalid_argument("Rich text input command is invalid");
}

TextOffset AfterDeletion(TextOffset offset, TextRange range) {
  if (offset <= range.start) return offset;
  return offset - std::min(offset - range.start, range.Length());
}

TextSelection AfterDeletion(TextSelection selection, TextRange range) {
  selection.anchor = AfterDeletion(selection.anchor, range);
  selection.active = AfterDeletion(selection.active, range);
  return selection;
}

TextRange AfterDeletion(TextRange target, TextRange range) {
  return {AfterDeletion(target.start, range), AfterDeletion(target.end, range)};
}

std::pair<TextRange, TextRange> SurroundingRanges(const Document& document, TextRange selection,
                                                  const TextInputCommand& command) {
  const auto length = document.Length();
  if (command.delete_unit == TextInputUnit::Utf16CodeUnit) {
    return {{selection.start - std::min(selection.start, command.delete_before), selection.start},
            {selection.end, selection.end + std::min(length - selection.end, command.delete_after)}};
  }
  std::vector<TextOffset> boundaries{0};
  for (const unsigned char byte : document.TextInRange({0, length})) {
    if ((byte & 0xC0U) != 0x80U) {
      boundaries.push_back(boundaries.back() + (byte >= 0xF0U ? 2 : 1));
    }
  }
  const auto start = std::lower_bound(boundaries.begin(), boundaries.end(), selection.start);
  const auto end = std::lower_bound(start, boundaries.end(), selection.end);
  const auto before = std::min<TextOffset>(command.delete_before, start - boundaries.begin());
  const auto after = std::min<TextOffset>(command.delete_after, boundaries.end() - end - 1);
  return {{*(start - before), selection.start}, {selection.end, *(end + after)}};
}

std::string ClipboardParagraphs(std::string_view text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\r') {
      normalized += text[i];
      continue;
    }
    normalized += '\n';
    if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
  }
  return normalized;
}

} // namespace

struct Document::Storage {
  explicit Storage(std::vector<Paragraph> value) : paragraphs(std::move(value)) {
    if (paragraphs.empty()) throw std::invalid_argument("Rich text document requires a paragraph");
    starts.reserve(paragraphs.size());
    for (const auto& paragraph : paragraphs) {
      const auto separator = starts.empty() ? 0 : 1;
      if (std::numeric_limits<TextOffset>::max() - length - separator < paragraph.text.Length()) {
        throw std::length_error("Rich text document exceeds the supported length");
      }
      length += separator;
      starts.push_back(length);
      length += paragraph.text.Length();
    }
  }

  std::vector<Paragraph> paragraphs;
  std::vector<TextOffset> starts;
  TextOffset length = 0;
};

Document::Document(std::vector<Paragraph> paragraphs)
    : storage_(std::make_shared<const Storage>(std::move(paragraphs))) {}

std::span<const Paragraph> Document::Paragraphs() const noexcept { return storage_->paragraphs; }

TextOffset Document::Length() const noexcept { return storage_->length; }

TextOffset Document::Start(std::size_t index) const { return storage_->starts.at(index); }

ParagraphPosition Document::Locate(TextOffset offset) const {
  if (offset < 0 || offset > Length()) throw std::invalid_argument("Rich text offset is out of bounds");
  const auto next = std::upper_bound(storage_->starts.begin(), storage_->starts.end(), offset);
  const auto index = static_cast<std::size_t>(next - storage_->starts.begin() - 1);
  const auto local = offset - Start(index);
  static_cast<void>(Paragraphs()[index].text.TextInRange({local, local}));
  return {index, local};
}

std::string Document::TextInRange(TextRange range) const {
  if (!range.IsValid()) throw std::invalid_argument("Rich text range must be ordered");
  const auto first = Locate(range.start);
  const auto last = Locate(range.end);
  std::string text;
  for (auto index = first.index; index <= last.index; ++index) {
    const auto& paragraph = Paragraphs()[index].text;
    text += paragraph.TextInRange({index == first.index ? first.offset : 0,
                                   index == last.index ? last.offset : paragraph.Length()});
    if (index < last.index) text += '\n';
  }
  return text;
}

EditSession::EditSession(std::vector<AttributedText> paragraphs)
    : current_{InitialDocument(std::move(paragraphs)), {}}, next_id_(current_.document.Paragraphs().back().id + 1) {}

void EditSession::SetSelection(TextSelection selection) {
  ValidateSelection(current_.document, selection);
  current_.selection = selection;
}

void EditSession::Replace(TextRange target, std::string_view text, std::optional<TextSelection> selection_after) {
  if (composition_) throw std::logic_error("Rich text replacement requires finishing composition");
  auto next_id = next_id_;
  auto next = Replaced(current_, target, AttributedText(std::string(text)), selection_after, next_id);
  if (next.document != current_.document) RecordUndo(current_);
  current_ = std::move(next);
  next_id_ = next_id;
}

void EditSession::ApplyCommands(std::span<const TextInputCommand> commands) {
  RequireCommand(!commands.empty());
  auto candidate = *this;
  candidate.undo_.clear();
  candidate.redo_.clear();
  std::optional<Snapshot> before;
  for (const auto& command : commands) {
    candidate.ApplyCommand(command);
    if (!before && !candidate.undo_.empty()) before = candidate.undo_.front();
    candidate.undo_.clear();
  }
  if (before) RecordUndo(*before);
  current_ = std::move(candidate.current_);
  next_id_ = candidate.next_id_;
  composition_ = candidate.composition_;
  composition_baseline_ = std::move(candidate.composition_baseline_);
}

bool EditSession::CanPerform(TextEditingAction action, PlatformClipboard* clipboard) const {
  switch (action) {
  case TextEditingAction::SelectAll: return current_.document.Length() != 0;
  case TextEditingAction::Copy:
  case TextEditingAction::Cut: return clipboard && !current_.selection.IsCollapsed();
  case TextEditingAction::Paste: return clipboard != nullptr;
  }
  return false;
}

bool EditSession::Perform(TextEditingAction action, PlatformClipboard* clipboard) {
  if (!CanPerform(action, clipboard)) return false;
  const auto range = current_.selection.Range();
  if (action == TextEditingAction::SelectAll) {
    SetSelection({0, current_.document.Length()});
    return true;
  }
  std::string replacement;
  if (action == TextEditingAction::Paste) {
    const auto text = clipboard->ReadText();
    if (!text) return false;
    replacement = ClipboardParagraphs(*text);
  } else {
    if (!clipboard->WriteText(current_.document.TextInRange(range))) return false;
    if (action == TextEditingAction::Copy) return true;
  }
  TextInputCommand finish;
  finish.kind = TextInputCommandKind::FinishComposition;
  TextInputCommand commit;
  commit.kind = TextInputCommandKind::CommitText;
  commit.target = range;
  commit.text = std::move(replacement);
  const std::array commands{std::move(finish), std::move(commit)};
  ApplyCommands(commands);
  return true;
}

void EditSession::ApplyCommand(const TextInputCommand& command) {
  const auto absolute = command.coordinate_space == TextInputCoordinateSpace::Text;
  const auto default_delete = command.delete_before == 0 && command.delete_after == 0 &&
      command.delete_unit == TextInputUnit::Utf16CodeUnit;
  const auto selection = current_.selection.Range();
  switch (command.kind) {
  case TextInputCommandKind::SetSelection:
    RequireCommand(absolute && default_delete && !command.target && command.selection_after && command.text.empty());
    SetSelection(*command.selection_after);
    return;
  case TextInputCommandKind::BeginComposition:
    RequireCommand(absolute && default_delete && !composition_ && command.target &&
                   !command.selection_after && command.text.empty());
    BeginComposition(*command.target);
    return;
  case TextInputCommandKind::UpdateComposition: {
    RequireCommand(default_delete);
    auto target = command.target.value_or(composition_.value_or(selection));
    if (composition_ && command.target) {
      RequireCommand(command.coordinate_space == TextInputCoordinateSpace::Composition && target.IsValid() &&
                     target.end <= composition_->Length());
      target = {composition_->start + target.start, composition_->start + target.end};
    } else {
      RequireCommand(absolute);
    }
    UpdateComposition(target, command.text, command.selection_after);
    return;
  }
  case TextInputCommandKind::CommitText:
    RequireCommand(absolute && default_delete && (!composition_ || !command.target));
    if (composition_) {
      UpdateComposition(*composition_, command.text, command.selection_after);
      FinishComposition();
    } else {
      Replace(command.target.value_or(selection), command.text, command.selection_after);
    }
    return;
  case TextInputCommandKind::FinishComposition:
  case TextInputCommandKind::CancelComposition:
    RequireCommand(absolute && default_delete && !command.target && !command.selection_after && command.text.empty());
    if (command.kind == TextInputCommandKind::FinishComposition) FinishComposition();
    else CancelComposition();
    return;
  case TextInputCommandKind::DeleteSurrounding: {
    RequireCommand(absolute && !command.target && !command.selection_after && command.text.empty() &&
                   command.delete_before >= 0 && command.delete_after >= 0 &&
                   (command.delete_unit == TextInputUnit::Utf16CodeUnit ||
                    command.delete_unit == TextInputUnit::UnicodeCodePoint));
    const auto [before, after] = SurroundingRanges(current_.document, selection, command);
    Delete(after);
    Delete(before);
    return;
  }
  }
  RequireCommand(false);
}

void EditSession::Delete(TextRange range) {
  if (range.IsCollapsed()) return;
  const auto selection = AfterDeletion(current_.selection, range);
  if (!composition_) {
    Replace(range, {}, selection);
    return;
  }
  if (composition_->start <= range.start && range.end <= composition_->end) {
    UpdateComposition(range, {}, selection);
    return;
  }
  RequireCommand(range.end <= composition_->start || composition_->end <= range.start);
  auto next_id = next_id_;
  auto next = Replaced(current_, range, AttributedText{}, selection, next_id);
  auto baseline = *composition_baseline_;
  auto baseline_range = range;
  if (range.start >= composition_->end) {
    const auto delta = baseline.target.end - composition_->end;
    baseline_range = {range.start + delta, range.end + delta};
  }
  auto restored = Replaced(baseline.snapshot, baseline_range, AttributedText{},
      AfterDeletion(baseline.snapshot.selection, baseline_range), next_id);
  RecordUndo(baseline.snapshot);
  composition_baseline_ = CompositionBaseline{
    std::move(restored), AfterDeletion(baseline.target, baseline_range),
  };
  composition_ = AfterDeletion(*composition_, range);
  current_ = std::move(next);
  next_id_ = next_id;
}

void EditSession::SetBold(bool bold) {
  if (composition_) throw std::logic_error("Rich text formatting requires finishing composition");
  const auto selection = current_.selection.Range();
  if (selection.IsCollapsed()) return;
  const auto first = current_.document.Locate(selection.start);
  const auto last = current_.document.Locate(selection.end);
  const auto old = current_.document.Paragraphs();
  std::vector<Paragraph> paragraphs(old.begin(), old.end());
  bool changed = false;
  for (auto index = first.index; index <= last.index; ++index) {
    auto& text = paragraphs[index].text;
    const TextRange range{index == first.index ? first.offset : 0,
                         index == last.index ? last.offset : text.Length()};
    if (range.IsCollapsed()) continue;
    auto formatted = WithWeight(text, range, bold ? FontWeight::Bold : FontWeight::Regular);
    changed |= formatted != text;
    text = std::move(formatted);
  }
  if (changed) {
    Document document(std::move(paragraphs));
    RecordUndo(current_);
    current_.document = std::move(document);
  }
}

void EditSession::BeginComposition(TextRange target) {
  if (composition_) throw std::logic_error("Rich text composition is already active");
  if (!target.IsValid()) throw std::invalid_argument("Rich text composition range must be ordered");
  static_cast<void>(current_.document.Locate(target.start));
  static_cast<void>(current_.document.Locate(target.end));
  composition_baseline_ = CompositionBaseline{current_, target};
  composition_ = target;
}

void EditSession::UpdateComposition(TextRange target, std::string_view text,
                                    std::optional<TextSelection> selection_after) {
  const auto composition = composition_.value_or(target);
  if (target.start < composition.start || target.end > composition.end) {
    throw std::invalid_argument("Rich text replacement must be inside composition");
  }
  const AttributedText inserted{std::string(text)};
  auto next_id = next_id_;
  auto next = Replaced(current_, target, inserted, selection_after, next_id);
  if (!composition_) composition_baseline_ = CompositionBaseline{current_, target};
  composition_ = TextRange{composition.start, composition.end - target.Length() + inserted.Length()};
  if (!selection_after) next.selection = {composition_->end, composition_->end};
  current_ = std::move(next);
  next_id_ = next_id;
}

void EditSession::FinishComposition() {
  if (!composition_) return;
  const auto& baseline = composition_baseline_->snapshot;
  if (baseline.document != current_.document) RecordUndo(baseline);
  composition_.reset();
  composition_baseline_.reset();
}

bool EditSession::CancelComposition() {
  if (!composition_) return false;
  current_ = std::move(composition_baseline_->snapshot);
  composition_.reset();
  composition_baseline_.reset();
  return true;
}

void EditSession::RecordUndo(const Snapshot& previous) {
  undo_.push_back(previous);
  if (undo_.size() > 64) undo_.erase(undo_.begin());
  redo_.clear();
}

bool EditSession::Undo() {
  if (CancelComposition()) return true;
  if (undo_.empty()) return false;
  redo_.push_back(current_);
  current_ = std::move(undo_.back());
  undo_.pop_back();
  return true;
}

bool EditSession::Redo() {
  if (composition_ || redo_.empty()) return false;
  undo_.push_back(current_);
  current_ = std::move(redo_.back());
  redo_.pop_back();
  return true;
}

} // namespace huxerui::rich_text::detail
