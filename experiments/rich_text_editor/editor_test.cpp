#include "editor.h"
#include "test.h"

#include <huxerui/huxerui.h>
#include <huxerui/testing/ui_test.h>

using namespace huxerui;
using namespace huxerui::testing;

View App() {
  std::vector<AttributedText> paragraphs{
    AttributedText::FromRanges("First", {}, {{{0, 5}, Uri("https://example.com")}}),
    AttributedText("Second"),
  };
  for (unsigned i = 0; i < 100; ++i) paragraphs.emplace_back("Offscreen " + std::to_string(i));
  return rich_text::RichTextEditor(std::move(paragraphs));
}

int main() {
  return Run([] {
    Application application(App, {.show_debug_overlay = false});
    UiTest ui(application, {.viewport = {400, 220}});
    auto editor = ui.Find(UiSelector::Key("rich-text-editor"));
    const auto first = ui.Find(UiSelector::Text("First")).One().bounds;
    const auto second = ui.Find(UiSelector::Text("Second")).One().bounds;
    ui.Drag({first.x + 20.0F, first.y + first.height / 2.0F},
            {second.x + 20.0F, second.y + second.height / 2.0F},
            {.pointer = {.device_kind = PointerDeviceKind::Mouse}});
    CHECK(ui.ActiveTextInput().has_value());
    const auto initial = *ui.ActiveTextInput();
    CHECK(initial.selection.Range() == (TextRange{2, 8}));
    CHECK(ui.SendTextInput({initial.session_id, {
      {.kind = TextInputCommandKind::UpdateComposition, .text = "拼"},
    }}).result_code == TextInputResultCode::Ok);
    CHECK(ui.ActiveTextInput()->composition == (TextRange{2, 3}));
    CHECK(ui.SendTextInput({initial.session_id, {
      {.kind = TextInputCommandKind::CommitText, .text = "中文"},
    }}).result_code == TextInputResultCode::Ok);
    ui.Pump();
    CHECK(ui.Find(UiSelector::Text("Fi中文cond")).Exists());
    ui.PressKey(Key::Z, {.control = true});
    CHECK(ui.ActiveTextInput()->session_id == initial.session_id);
    CHECK(ui.ActiveTextInput()->selection == initial.selection);
    CHECK(ui.Find(UiSelector::Text("First")).Exists());
    CHECK(ui.Find(UiSelector::Text("Second")).Exists());
    const auto before_bold = ui.ActiveTextInput()->revision;
    ui.PressKey(Key::B, {.control = true});
    CHECK(ui.ActiveTextInput()->revision > before_bold);
    CHECK(ui.ActiveTextInput()->content_revision > initial.content_revision);
    CHECK(editor.ScrollUntil(UiSelector::Text("Offscreen 80"), {.step = {0.0F, 220.0F}}).Exists());
    CHECK(!ui.Find(UiSelector::Text("First")).Exists());
    CHECK(ui.ActiveTextInput()->selection == initial.selection);
    CHECK(ui.ActiveTextInput()->session_id == initial.session_id);
    ui.PressKey(Key::ArrowRight);
    CHECK(ui.ActiveTextInput()->selection.IsCollapsed());
    CHECK(ui.ActiveTextInput()->selection.active == initial.selection.Range().end);
  });
}
