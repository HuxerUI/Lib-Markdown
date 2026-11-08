#include "rich_text_editor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include <huxerui/huxerui.h>

#include "discard.h"

namespace huxerui::rich_text {
namespace {
using detail::Document;
using detail::EditSession;

Transform2D Transform(const ViewNode& node, Point origin = {}) {
  const Point start = node.LocalToWindow(origin);
  const Point x = node.LocalToWindow({origin.x + 1.0F, origin.y});
  const Point y = node.LocalToWindow({origin.x, origin.y + 1.0F});
  return {x.x - start.x, x.y - start.y, y.x - start.x, y.y - start.y, start.x, start.y};
}

struct RowGeometry {
  TextBlockId id = 0;
  std::size_t index = 0;
  AttributedText text;
  std::optional<TextParagraphLayout> layout;
  Transform2D transform;
  Rect bounds;
};

TextRange LocalRange(TextRange range, TextOffset start, TextOffset length) {
  return {std::clamp(range.start - start, TextOffset{0}, length),
          std::clamp(range.end - start, TextOffset{0}, length)};
}

template<class Result> Result QueryFailure(TextInputSessionId id,
                                           TextInputResultCode code = TextInputResultCode::Rejected) {
  Result failure{};
  failure.result_code = code;
  failure.session_id = id;
  return failure;
}

bool SameText(const Document& left, const Document& right) {
  return left.Length() == right.Length() && std::ranges::equal(left.Paragraphs(), right.Paragraphs(),
      [](const auto& a, const auto& b) { return a.text.PlainText() == b.text.PlainText(); });
}

AttributedText EditableText(const AttributedText& text, Color link_color) {
  const auto links = text.LinkRanges();
  if (links.empty()) return text;
  const auto styles = text.StyleRanges();
  std::vector<TextOffset> boundaries{0, text.Length()};
  for (const auto& range : styles) {
    boundaries.push_back(range.range.start);
    boundaries.push_back(range.range.end);
  }
  for (const auto& link : links) {
    boundaries.push_back(link.range.start);
    boundaries.push_back(link.range.end);
  }
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

  // Preserve link appearance while the document surface owns focus and input.
  auto next_style = styles.begin();
  auto next_link = links.begin();
  std::vector<TextStyleRange> result;
  for (std::size_t index = 1; index < boundaries.size(); ++index) {
    const TextRange range{boundaries[index - 1], boundaries[index]};
    while (next_style != styles.end() && next_style->range.end <= range.start) ++next_style;
    while (next_link != links.end() && next_link->range.end <= range.start) ++next_link;
    TextSpanStyle style;
    if (next_style != styles.end() && next_style->range.start <= range.start) style = next_style->style;
    if (next_link != links.end() && next_link->range.start <= range.start) {
      if (!style.foreground) style.foreground = link_color;
      if (!style.decoration) style.decoration = TextDecoration::Underline;
    }
    if (style != TextSpanStyle{}) result.push_back({range, std::move(style)});
  }
  return AttributedText::FromRanges(text.PlainText(), std::move(result));
}

class Editor final : public TextInputClient, public TextSelectionClient {
public:
  Editor(std::shared_ptr<EditSession> edit, huxerui::State<std::uint64_t> revision, ScrollController scroll)
      : edit_(std::move(edit)), view_revision_(std::move(revision)), scroll_(std::move(scroll)) {}

  void SetObservers(const detail::EditorOptions& options) {
    changed_ = options.changed;
    rejected_ = options.rejected;
  }

  void SetItemLookup(std::function<std::size_t(std::size_t)> lookup) { item_for_paragraph_ = std::move(lookup); }

  void SetScrollController(ScrollController scroll) { scroll_ = std::move(scroll); }

  const auto& Current() const { return edit_->Current(); }
  auto Composition() const { return edit_->Composition(); }
  bool Focused() const noexcept { return session_ != 0; }

  void ToggleFormat(detail::InlineFormat format, TextSelection selection) {
    Change([&] { edit_->ToggleFormat(format, selection); });
  }

  void Register(RowGeometry& row) { rows_[row.id] = &row; }

  void Unregister(const RowGeometry& row) {
    const auto entry = rows_.find(row.id);
    if (entry != rows_.end() && entry->second == &row) rows_.erase(entry);
  }

  void OwnerGeometry(Transform2D transform, Color selection, Rect viewport) {
    transform_ = transform;
    selection_color_ = selection;
    viewport_bounds_ = viewport;
  }

  void RevealIfNeeded() {
    if (!reveal_) return;
    const auto reveal_offset = reveal_offset_;
    reveal_ = false;
    reveal_offset_.reset();
    // A user scroll since the request makes the old caret reveal stale.
    if (reveal_offset && *reveal_offset != scroll_.Offset()) return;
    const auto position = Current().document.Locate(Current().selection.active);
    if (!Mounted(position.index)) {
      scroll_.ScrollToItem(item_for_paragraph_ ? item_for_paragraph_(position.index) : position.index);
    }
  }

  void CancelReveal() {
    reveal_ = false;
    reveal_offset_.reset();
  }

  void ViewportScrolled() override { CancelReveal(); }

  TextInputConfiguration Configuration() const override { return {.multiline = true}; }

  TextInputState State() const override {
    return {session_, revision_, content_revision_, Current().selection, Composition()};
  }

  TextInputState BeginTextInput(TextInputSessionId id) override {
    session_ = id;
    Refresh();
    return State();
  }

  void EndTextInput(TextInputSessionId id, TextInputEndReason reason) override {
    if (session_ != id) return;
    session_ = 0;
    if (reason == TextInputEndReason::RuntimeDestroyed || reason == TextInputEndReason::ClientRemoved) {
      edit_->FinishComposition();
      return;
    }
    const auto composing = Composition().has_value();
    Change([&] { edit_->FinishComposition(); });
    if (!composing) Refresh();
  }

  TextInputApplyResult ApplyTextInput(const TextInputCommandBatch& batch) override {
    if (!Focused() || batch.session_id != session_) return {TextInputResultCode::SessionMismatch};
    return {Change([&] { edit_->ApplyCommands(batch.commands); }, true)
                ? TextInputResultCode::Ok : TextInputResultCode::Rejected};
  }

  TextInputContext QueryTextInputContext(TextInputSessionId id, TextOffset start, TextOffset length) const override {
    if (!Focused() || session_ != id) return QueryFailure<TextInputContext>(id, TextInputResultCode::SessionMismatch);
    if (start < 0 || length < 0) return QueryFailure<TextInputContext>(id);
    const auto& document = Current().document;
    start = std::min(start, document.Length());
    auto end = start + std::min(length, document.Length() - start);
    // Platform requests may end inside a supplementary scalar.
    try { LIB_MARKDOWN_DISCARD(document.Locate(start)); } catch (const std::invalid_argument&) { --start; }
    try { LIB_MARKDOWN_DISCARD(document.Locate(end)); } catch (const std::invalid_argument&) { ++end; }
    return {
      TextInputResultCode::Ok, id, start, document.Length(), document.TextInRange({start, end}),
      Current().selection, Composition(),
    };
  }

  TextInputGeometry QueryTextInputGeometry(TextInputSessionId id, TextRange range) const override {
    if (!Focused() || session_ != id) return QueryFailure<TextInputGeometry>(id, TextInputResultCode::SessionMismatch);
    try {
      if (!range.IsValid()) return QueryFailure<TextInputGeometry>(id);
      LIB_MARKDOWN_DISCARD(Current().document.Locate(range.start));
      const auto affinity = range.end == Current().selection.active ? Current().selection.affinity
                                                                     : TextAffinity::Downstream;
      if (const auto caret = Caret({range.end, affinity})) {
        // Cached rows can be outside the viewport; do not let the platform reveal them.
        if (!viewport_bounds_.IsEmpty() && !viewport_bounds_.Intersects(*caret)) return QueryFailure<TextInputGeometry>(id);
        return {TextInputResultCode::Ok, id, *caret, Rectangles(range)};
      }
    } catch (const std::invalid_argument&) {
      return QueryFailure<TextInputGeometry>(id);
    }
    return QueryFailure<TextInputGeometry>(id);
  }

  TextInputPositionResult QueryTextInputPosition(TextInputSessionId id, Point point) const override {
    if (!Focused() || session_ != id) {
      return QueryFailure<TextInputPositionResult>(id, TextInputResultCode::SessionMismatch);
    }
    if (const auto position = Hit(point)) return {TextInputResultCode::Ok, id, *position};
    return QueryFailure<TextInputPositionResult>(id);
  }

  std::optional<TextPosition> Hit(Point point) const {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return {};
    const auto window = transform_.Apply(point);
    const auto& document = Current().document;
    const RowGeometry* best = nullptr;
    std::size_t best_index = 0;
    float distance = std::numeric_limits<float>::infinity();
    Point local;
    for (const auto& entry : rows_) {
      const auto* row = entry.second;
      if (row->index >= document.Paragraphs().size() || Mounted(row->index) != row) continue;
      const auto hit = row->transform.Inverse(window);
      if (!hit) continue;
      const auto dx = std::max({0.0F, -hit->x, hit->x - row->bounds.width});
      const auto dy = std::max({0.0F, -hit->y, hit->y - row->bounds.height});
      const auto outside = std::hypot(dx, dy);
      if (outside < distance) {
        distance = outside;
        local = *hit;
        best = row;
        best_index = row->index;
      }
    }
    if (!best) return {};
    auto position = best->layout->HitTest(local);
    position.offset += document.Start(best_index);
    return position;
  }

  bool Place(Point point, bool extend) {
    const auto hit = Hit(point);
    if (!hit) return false;
    Change([&] {
      edit_->FinishComposition();
      edit_->SetSelection({extend ? Current().selection.anchor : hit->offset, hit->offset, hit->affinity});
    });
    return true;
  }

  TextInputKeyResult HandleTextKey(const KeyEvent& event) override {
    constexpr auto handled = TextInputKeyResult::Handled;
    constexpr auto unhandled = TextInputKeyResult::Unhandled;
    if (event.type != KeyEventType::Down) return unhandled;
    if (event.modifiers.control || event.modifiers.meta) {
      if (event.key == Key::Z || event.key == Key::Y) {
        Change([&] { (event.modifiers.shift || event.key == Key::Y) ? edit_->Redo() : edit_->Undo(); }, true);
        return handled;
      }
      if (event.key == Key::B && !Composition()) {
        if (Current().document.Backing()) ToggleFormat(detail::InlineFormat::Bold, Current().selection);
        else Change([&] { edit_->SetBold(!SelectionStartsBold()); });
        return handled;
      }
      if (event.key == Key::I && !Composition() && Current().document.Backing()) {
        ToggleFormat(detail::InlineFormat::Italic, Current().selection);
        return handled;
      }
      return unhandled;
    }
    if (Composition() || event.modifiers.alt) return unhandled;
    const auto selection = Current().selection;
    auto range = selection.Range();
    if (event.key == Key::Enter || event.key == Key::NumpadEnter) {
      Change([&] { edit_->Replace(range, "\n"); }, true);
      return handled;
    }
    if (event.key == Key::Backspace || event.key == Key::Delete) {
      if (range.IsCollapsed()) {
        const auto neighbor = Neighbor(selection.active, event.key == Key::Delete);
        range = {std::min(selection.active, neighbor), std::max(selection.active, neighbor)};
      }
      Change([&] { edit_->Replace(range, {}); }, true);
      return handled;
    }
    if (event.key == Key::ArrowLeft || event.key == Key::ArrowRight) {
      const bool forward = event.key == Key::ArrowRight;
      const auto offset = selection.IsCollapsed() || event.modifiers.shift ? Neighbor(selection.active, forward)
                                                                            : forward ? range.end : range.start;
      Move(offset, TextAffinity::Downstream, event.modifiers.shift);
      return handled;
    }
    if (event.key == Key::ArrowUp || event.key == Key::ArrowDown) {
      if (const auto caret = Caret({selection.active, selection.affinity})) {
        const float step = event.key == Key::ArrowUp ? -0.5F : 1.5F;
        if (const auto hit = Hit({caret->x, caret->y + step * caret->height})) {
          Move(hit->offset, hit->affinity, event.modifiers.shift);
        }
      }
      return handled;
    }
    return unhandled;
  }

  bool CanPerformTextEditingAction(TextEditingAction action, PlatformClipboard* clipboard) const override {
    return edit_->CanPerform(action, clipboard);
  }

  bool PerformTextEditingAction(TextEditingAction action, PlatformClipboard* clipboard) override {
    bool performed = false;
    Change([&] { performed = edit_->Perform(action, clipboard); },
           action == TextEditingAction::Paste || action == TextEditingAction::Cut);
    return performed;
  }

  bool ClearSelection() override {
    const auto selection = Current().selection;
    if (selection.IsCollapsed()) return false;
    Move(selection.active, selection.affinity, false);
    return true;
  }

  bool SelectWord(Point point) override {
    const auto hit = Hit(point);
    if (!hit) return false;
    const auto end = Neighbor(hit->offset, true);
    const auto start = end == hit->offset ? Neighbor(end, false) : hit->offset;
    if (start == end) return false;
    Change([&] { edit_->SetSelection({start, end}); });
    return true;
  }

  bool ExtendSelection(Point point, bool start_handle) override {
    const auto hit = Hit(point);
    if (!hit) return false;
    const auto range = Current().selection.Range();
    Change([&] {
      if (start_handle) edit_->SetSelection({range.end, std::min(hit->offset, range.end), hit->affinity});
      else edit_->SetSelection({range.start, std::max(hit->offset, range.start), hit->affinity});
    });
    return true;
  }

  std::optional<TextSelectionGeometry> QuerySelectionGeometry() const override {
    const auto selection = Current().selection;
    if (selection.IsCollapsed() && !Focused()) return {};
    const auto range = selection.Range();
    const auto rects = Rectangles(range);
    const auto start_affinity = selection.active == range.start ? selection.affinity : TextAffinity::Downstream;
    const auto end_affinity = selection.active == range.end ? selection.affinity : TextAffinity::Upstream;
    return TextSelectionGeometry{
      Caret({range.start, start_affinity}), Caret({range.end, end_affinity}),
      rects.empty() ? std::nullopt : std::optional(rects.front()),
    };
  }

  Color SelectionHandleColor() const noexcept override { return selection_color_; }

private:
  template<class Function> bool Change(Function&& function, bool reveal = false) {
    const auto before = Current();
    const auto composition = Composition();
    try {
      function();
    } catch (const std::invalid_argument& error) {
      if (rejected_) rejected_(error.what());
      return false;
    } catch (const std::length_error& error) {
      if (rejected_) rejected_(error.what());
      return false;
    } catch (const std::out_of_range& error) {
      if (rejected_) rejected_(error.what());
      return false;
    } catch (const std::runtime_error& error) {
      if (rejected_) rejected_(error.what());
      return false;
    }
    const bool document_changed = before.document != Current().document;
    const bool composition_changed = composition != Composition();
    if (document_changed && !SameText(before.document, Current().document)) ++content_revision_;
    if (document_changed || before.selection != Current().selection || composition_changed) {
      ++revision_;
      reveal_ |= reveal;
      if (reveal) reveal_offset_ = scroll_.Offset();
      Refresh();
    }
    if ((document_changed || before.selection != Current().selection || composition_changed) && changed_)
      changed_(Current().document, Current().selection, Composition());
    return true;
  }

  void Refresh() { ++view_revision_; }

  void Move(TextOffset offset, TextAffinity affinity, bool extend) {
    Change([&] { edit_->SetSelection({extend ? Current().selection.anchor : offset, offset, affinity}); }, true);
  }

  bool SelectionStartsBold() const {
    const auto& document = Current().document;
    const auto start = document.Locate(Current().selection.Range().start);
    for (const auto& range : document.Paragraphs()[start.index].text.StyleRanges()) {
      if (range.range.start <= start.offset && start.offset < range.range.end) {
        return range.style.font_weight == FontWeight::Bold;
      }
    }
    return false;
  }

  const RowGeometry* Mounted(std::size_t index) const {
    const auto& block = Current().document.Paragraphs()[index];
    const auto found = rows_.find(block.id);
    if (found == rows_.end() || !found->second->layout || found->second->text != block.text) return nullptr;
    return found->second;
  }

  TextOffset Neighbor(TextOffset offset, bool forward) const {
    const auto& document = Current().document;
    const auto position = document.Locate(offset);
    const auto& paragraph = document.Paragraphs()[position.index].text;
    if (forward && position.offset == paragraph.Length()) return offset < document.Length() ? offset + 1 : offset;
    if (!forward && position.offset == 0) return offset > 0 ? offset - 1 : offset;
    const auto* row = Mounted(position.index);
    if (!row) {
      scroll_.ScrollToItem(position.index);
      return offset;
    }
    const auto local = forward ? row->layout->NextCaretOffset(position.offset)
                               : row->layout->PreviousCaretOffset(position.offset);
    return document.Start(position.index) + local;
  }

  std::optional<Rect> ToOwner(const RowGeometry& row, Rect local) const {
    const std::array corners{
      Point{local.x, local.y}, Point{local.x + local.width, local.y},
      Point{local.x, local.y + local.height}, Point{local.x + local.width, local.y + local.height},
    };
    auto left = std::numeric_limits<float>::infinity();
    auto top = left;
    auto right = -left;
    auto bottom = right;
    for (const auto point : corners) {
      const auto mapped = transform_.Inverse(row.transform.Apply(point));
      if (!mapped) return {};
      left = std::min(left, mapped->x);
      top = std::min(top, mapped->y);
      right = std::max(right, mapped->x);
      bottom = std::max(bottom, mapped->y);
    }
    return Rect{left, top, right - left, bottom - top};
  }

  std::optional<Rect> Caret(TextPosition position) const {
    const auto located = Current().document.Locate(position.offset);
    const auto* row = Mounted(located.index);
    if (!row) return {};
    return ToOwner(*row, row->layout->CaretRect({located.offset, position.affinity}));
  }

  std::vector<Rect> Rectangles(TextRange range) const {
    std::vector<Rect> rects;
    if (range.IsCollapsed()) return rects;
    const auto& document = Current().document;
    const auto first = document.Locate(range.start).index;
    const auto last = document.Locate(range.end).index;
    for (const auto& entry : rows_) {
      const auto* row = entry.second;
      if (row->index < first || row->index > last || Mounted(row->index) != row) continue;
      const auto local = LocalRange(range, document.Start(row->index), row->text.Length());
      for (const auto rect : row->layout->RangeRects(local)) {
        if (const auto mapped = ToOwner(*row, rect)) rects.push_back(*mapped);
      }
    }
    return rects;
  }

  std::shared_ptr<EditSession> edit_;
  huxerui::State<std::uint64_t> view_revision_;
  ScrollController scroll_;
  std::function<void(const Document&, TextSelection, std::optional<TextRange>)> changed_;
  std::function<void(std::string_view)> rejected_;
  std::function<std::size_t(std::size_t)> item_for_paragraph_;
  std::map<TextBlockId, RowGeometry*> rows_;
  Transform2D transform_;
  Rect viewport_bounds_;
  Color selection_color_{};
  TextInputSessionId session_ = 0;
  std::uint64_t revision_ = 0;
  std::uint64_t content_revision_ = 0;
  bool reveal_ = true;
  std::optional<float> reveal_offset_;
};

struct DocumentSurface {
  class Extension;
  std::shared_ptr<Editor> editor;
  Color selection;
};

class DocumentSurface::Extension final : public NodeExtension {
public:
  Extension(ViewNode&, const DocumentSurface& value) : value_(value) {}

  void Update(ViewNode&, const DocumentSurface& value) {
    value_ = value;
    InvalidateSemantics();
  }

  PaintInvalidation PrepareGeometry(ViewNode& node, TextMeasurer&) override {
    value_.editor->OwnerGeometry(Transform(node), value_.selection, node.ContentBounds());
    return PaintInvalidation::None;
  }

  std::shared_ptr<TextInputClient> GetTextInputClient() noexcept override { return value_.editor; }
  TextSelectionClient* GetTextSelectionClient() noexcept override { return value_.editor.get(); }

  bool HitTest(ViewNode& node, Point position) const override {
    return node.ContentBounds().Contains(position);
  }

  PointerResult OnPointer(ViewNode&, const PointerEvent& event) override {
    if (event.type == PointerEventType::Down && event.changed_button == PointerButton::Primary &&
        event.device_kind == PointerDeviceKind::Mouse && value_.editor->Place(event.position, event.modifiers.shift)) {
      drag_ = event.pointer_id;
      return PointerResult::Capture;
    }
    if (drag_ != event.pointer_id) return PointerResult::Ignored;
    if (event.type == PointerEventType::Move || event.type == PointerEventType::Up) {
      value_.editor->Place(event.position, true);
    }
    if (event.type == PointerEventType::Up || event.type == PointerEventType::Cancel) drag_.reset();
    return PointerResult::Handled;
  }

  float OnPreScroll(ViewNode&, Axis, float, ScrollSource source) override {
    if (source != ScrollSource::FocusReveal) value_.editor->CancelReveal();
    return 0.0F;
  }

  void OnScrollActivity(ViewNode&, const ScrollActivity& activity) override {
    if (activity.source != ScrollSource::FocusReveal) value_.editor->CancelReveal();
  }

  void BuildSemantics(SemanticBuilder& builder) const override {
    const auto& current = value_.editor->Current();
    builder.SetOwner({
      .role = SemanticRole::TextField,
      .label = "Rich text editor",
      .value = current.document.TextInRange({0, current.document.Length()}),
      .text_selection = current.selection.Range(),
    });
  }

private:
  DocumentSurface value_;
  std::optional<std::int64_t> drag_;
};

struct ParagraphSurface {
  class Extension;
  std::shared_ptr<Editor> editor;
  detail::Paragraph paragraph;
  AttributedText displayed;
  std::size_t index;
  TextOffset start;
  TextStyle style;
  TextShapingOptions shaping;
  TextAlign align;
  Color selection_color;
  Color caret_color;
};

class ParagraphSurface::Extension final : public NodeExtension {
public:
  Extension(ViewNode&, const ParagraphSurface& value) : value_(value) {}

  ~Extension() override { value_.editor->Unregister(row_); }

  void Update(ViewNode&, const ParagraphSurface& value) {
    if (value_.editor != value.editor || value_.paragraph.id != value.paragraph.id) {
      value_.editor->Unregister(row_);
      row_.layout.reset();
    }
    row_.index = value.index;
    value_ = value;
    InvalidatePaint(PaintInvalidation::Both);
  }

  PaintInvalidation PrepareGeometry(ViewNode& node, TextMeasurer& measurer) override {
    const auto bounds = node.ContentBounds();
    row_ = {
      value_.paragraph.id, value_.index, value_.paragraph.text,
      measurer.LayoutText(value_.displayed, value_.style, bounds.width, {.shaping = value_.shaping, .align = value_.align}),
      Transform(node, {bounds.x, bounds.y}), bounds,
    };
    value_.editor->Register(row_);
    return PaintInvalidation::Both;
  }

  void PaintBehindContent(const ViewNode& node, PaintContext& context) const override {
    if (!row_.layout || row_.text != value_.paragraph.text) return;
    context.PushClip(node.ContentBounds());
    const auto range = LocalRange(value_.editor->Current().selection.Range(), value_.start, row_.text.Length());
    for (auto rect : row_.layout->RangeRects(range)) {
      context.DrawRect(Offset(rect), value_.selection_color);
    }
    context.PopClip();
  }

  void PaintAboveContent(const ViewNode& node, PaintContext& context) const override {
    if (!row_.layout || row_.text != value_.paragraph.text || !value_.editor->Focused()) return;
    const auto selection = value_.editor->Current().selection;
    const auto active = selection.active - value_.start;
    const auto position = value_.editor->Current().document.Locate(selection.active);
    const auto bounds = node.ContentBounds();
    context.PushClip(bounds);
    if (const auto composition = value_.editor->Composition()) {
      const auto range = LocalRange(*composition, value_.start, row_.text.Length());
      for (auto rect : row_.layout->RangeRects(range)) {
        rect.y += std::max(0.0F, rect.height - 1.0F);
        rect.height = 1.0F;
        context.DrawRect(Offset(rect), value_.caret_color);
      }
    }
    if (selection.IsCollapsed() && value_.editor->Current().document.Paragraphs()[position.index].id == row_.id) {
      auto caret = row_.layout->CaretRect({active, selection.affinity});
      caret.width = std::min(2.0F, bounds.width);
      caret.x = std::clamp(caret.x, 0.0F, bounds.width - caret.width);
      context.DrawRect(Offset(caret), value_.caret_color);
    }
    context.PopClip();
  }

private:
  Rect Offset(Rect rect) const {
    rect.x += row_.bounds.x;
    rect.y += row_.bounds.y;
    return rect;
  }

  ParagraphSurface value_;
  RowGeometry row_;
};

View MakeEditor(std::function<std::shared_ptr<EditSession>()> initialize, detail::EditorOptions options = {}) {
  return Scope([initialize = std::move(initialize), options = std::move(options)]() -> View {
    auto revision = UseState(std::uint64_t{0});
    const auto owner = UseState(std::make_shared<std::shared_ptr<Editor>>()).Get();
    const auto retained_scroll = UseScrollController();
    const auto scroll = options.scroll_controller.value_or(retained_scroll);
    if (!*owner) *owner = std::make_shared<Editor>(initialize(), revision, scroll);
    const auto editor = *owner;
    editor->SetScrollController(scroll);
    editor->SetObservers(options);
    if (options.format_actions) {
      const std::weak_ptr<Editor> weak = editor;
      options.format_actions->selection = [weak]() -> std::optional<TextSelection> {
        if (const auto editor = weak.lock()) return editor->Current().selection;
        return std::nullopt;
      };
      options.format_actions->toggle = [weak](detail::InlineFormat format, TextSelection selection) {
        if (const auto editor = weak.lock()) editor->ToggleFormat(format, selection);
      };
    }
    Lifecycle([editor] { editor->RevealIfNeeded(); }, revision.Get());
    const auto document = editor->Current().document;
    const auto& theme = UseTheme();
    const auto style = options.base_style.value_or(
        TextStyle{Font::FromFamily(theme.typography.family, theme.typography.body_large), theme.colors.on_surface});
    const TextShapingOptions shaping{.locale = std::string(UseEnvironment<Locale>().LanguageTag())};
    auto selection = theme.colors.primary;
    selection.alpha *= 0.26F;
    const auto caret = theme.colors.primary;
    const auto line_height = UseTextMeasurer().Metrics(style.font).LineHeight();
    const auto spacing = options.paragraph_spacing;
    const auto layout = options.layout ? options.layout(document) : detail::EditorLayout{};
    editor->SetItemLookup(layout.item_for_paragraph);
    const auto count = options.layout ? layout.item_ids.size() : document.Paragraphs().size();
    const detail::RenderParagraph render = [=](std::size_t index, AttributedText displayed, TextStyle base,
                                                TextAlign align) -> View {
      const auto& paragraph = document.Paragraphs()[index];
      return Text(displayed).Style(base).Shaping(shaping).Align(align)
          .With(Frame{.min_height = line_height},
                ParagraphSurface{editor, paragraph, displayed, index, document.Start(index), base, shaping, align,
                                 selection, caret}).Key(paragraph.id);
    };
    return VirtualList(count, [=](std::size_t index) -> View {
      return Scope([=] {
        View row = layout.render_item ? layout.render_item(index, render) :
            render(index, EditableText(document.Paragraphs()[index].text, caret), style, TextAlign::Leading);
        const float gap = index + 1 < count ? spacing : 0.0F;
        return std::move(row).With(Padding(EdgeInsets{0.0F, 0.0F, gap, 0.0F}));
      }).Key(layout.render_item ? layout.item_ids[index] : document.Paragraphs()[index].id);
    }).EstimatedItemExtent(options.estimated_item_extent > 0.0F ? options.estimated_item_extent : line_height + spacing)
      .CacheExtent(options.cache_extent).Controller(scroll)
      .With(Focusable{true}, PointerCursor(PointerCursorKind::Text), DocumentSurface{editor, caret}, ScrollBar(),
            Grow{}).Key("rich-text-editor");
  });
}

} // namespace

View RichTextEditor(std::vector<AttributedText> paragraphs) {
  return MakeEditor([paragraphs = std::move(paragraphs)] { return std::make_shared<EditSession>(paragraphs); });
}

View detail::RichTextEditor(std::shared_ptr<EditSession> edit, EditorOptions options) {
  return MakeEditor([edit = std::move(edit)] { return edit; }, std::move(options));
}

} // namespace huxerui::rich_text
