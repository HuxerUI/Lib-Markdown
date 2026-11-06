#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <huxerui/text.h>
#include <huxerui/text_input.h>

namespace huxerui::rich_text::detail {

struct Paragraph {
  TextBlockId id;
  AttributedText text;
};

struct ParagraphPosition {
  std::size_t index;
  TextOffset offset;
};

class DocumentBacking;
enum class InlineFormat { Bold, Italic, Code };

// UTF-16 document offsets count a single LF between each pair of paragraphs.
// Unaffected paragraphs share their attributed text with previous snapshots.
class Document {
public:
  explicit Document(std::vector<Paragraph> paragraphs, std::shared_ptr<const DocumentBacking> backing = {});
  [[nodiscard]] std::span<const Paragraph> Paragraphs() const noexcept;
  [[nodiscard]] TextOffset Length() const noexcept;
  [[nodiscard]] TextOffset Start(std::size_t index) const;
  [[nodiscard]] ParagraphPosition Locate(TextOffset offset) const;
  [[nodiscard]] std::string TextInRange(TextRange range) const;
  [[nodiscard]] const std::shared_ptr<const DocumentBacking>& Backing() const noexcept;
  bool operator==(const Document&) const noexcept = default;

private:
  struct Storage;
  std::shared_ptr<const Storage> storage_;
};

// A source-backed editor publishes projections from accepted document edits.
// The backing snapshot is kept alongside the displayed paragraphs in history.
class DocumentBacking {
public:
  virtual ~DocumentBacking() = default;
  [[nodiscard]] virtual Document Replace(const Document& document, TextRange target,
                                         std::string_view replacement, TextBlockId& next_id) const = 0;
  [[nodiscard]] virtual Document ToggleFormat(const Document& document, TextRange target, InlineFormat format) const = 0;
};

struct Snapshot {
  Document document;
  TextSelection selection;
};

// The edit session owns document changes and history independently of any View.
// Composition updates can span paragraphs and publish one history entry on finish.
class EditSession {
public:
  explicit EditSession(std::vector<AttributedText> paragraphs = {});
  explicit EditSession(Document document);
  [[nodiscard]] const Snapshot& Current() const noexcept { return current_; }
  [[nodiscard]] std::optional<TextRange> Composition() const noexcept { return composition_; }

  void SetSelection(TextSelection selection);
  void Replace(TextRange target, std::string_view text, std::optional<TextSelection> selection_after = {});
  void ApplyCommands(std::span<const TextInputCommand> commands);
  [[nodiscard]] bool CanPerform(TextEditingAction action, PlatformClipboard* clipboard) const;
  bool Perform(TextEditingAction action, PlatformClipboard* clipboard);
  void SetBold(bool bold);
  void ToggleFormat(InlineFormat format, TextSelection selection);
  void BeginComposition(TextRange target);
  void UpdateComposition(TextRange target, std::string_view text, std::optional<TextSelection> selection_after = {});
  void FinishComposition();
  bool CancelComposition();
  bool Undo();
  bool Redo();

private:
  struct CompositionBaseline {
    Snapshot snapshot;
    TextRange target;
  };

  void ApplyCommand(const TextInputCommand& command);
  void Delete(TextRange range);
  void RecordUndo(const Snapshot& previous);
  Snapshot current_;
  TextBlockId next_id_;
  std::optional<TextRange> composition_;
  std::optional<CompositionBaseline> composition_baseline_;
  std::vector<Snapshot> undo_;
  std::vector<Snapshot> redo_;
};

} // namespace huxerui::rich_text::detail
