#include <huxerui/huxerui.h>
#include <huxerui/markdown.h>
#include <huxerui/testing/ui_test.h>
#include "test.h"

using namespace huxerui;
using namespace huxerui::markdown;
using namespace huxerui::testing;

State<TextEditingValue> source;
ScrollController scroll;
std::string initial, error;
View EditorRoot() {
  source = UseState(TextEditingValue::FromText(initial));
  return ProvideEnvironment(TextSelectionMenuLabels{"Cut", "Copy", "Paste", "Select all"},
      MaterialTheme(MaterialLightThemeSpec(), Column{
        Button("outside").Key("outside"),
        MarkdownEditor(source, {.parse_options = {.wiki_links = true, .preserve_blank_lines = true},
            .virtualization = {.cache_extent = 100, .scroll_controller = scroll}, .rich_text = false})
            .On<MarkdownEditorEvents::Changed>([](const TextEditingValue& next) { source = next; })
            .On<MarkdownEditorEvents::Error>([](const std::string& message) { error = message; })
            .With(Grow{}).Key("editor"),
      }.With(CrossAlign(CrossAxisAlignment::Stretch))));
}

int main() { return Run([] {
  initial = "# heading\n\nfirst **bold**\n\nsecond😀\n\n- read only\n";
  Application app(EditorRoot, {.show_debug_overlay = false});
  UiTest ui(app, {.viewport = {650, 550}});
  CHECK(ui.Find(UiSelector::Text("first bold")).Exists());
  ui.Find(UiSelector::Text("first bold")).Tap(); // One tap must start native input.
  auto input = ui.Find(UiSelector::AllOf(UiSelector::Key("markdown-block-input"), UiSelector::Focused(true)));
  CHECK(input.Exists());
  CHECK(input.One().value == "first **bold**");
  const auto active_session = ui.ActiveTextInput()->session_id;
  input.ReplaceText("中文😀 **实时**");
  CHECK(source.Get().text == "# heading\n\n中文😀 **实时**\n\nsecond😀\n\n- read only\n");
  CHECK(ui.Find(UiSelector::Key("markdown-block-preview")).Find(UiSelector::Text("中文😀 实时")).Exists());
  CHECK(ui.ActiveTextInput()->session_id == active_session);
  std::string expected = "中文😀 实时";
  for (int i = 0; i < 8; ++i) {
    input.EnterText("x");
    expected += "x";
    ui.Pump(std::chrono::milliseconds(30));
    CHECK(ui.Find(UiSelector::Key("markdown-block-preview")).Find(UiSelector::Text(expected)).Exists());
    CHECK(ui.ActiveTextInput()->session_id == active_session);
  }
  input.ReplaceText("中文😀 **实时**");
  input.SetSelection({4, 4});
  input.EnterText("x");
  CHECK(input.One().value == "中文😀x **实时**");
  CHECK(ui.ActiveTextInput()->session_id == active_session);
  // Native composition remains on the same mounted client through parsing.
  CHECK(ui.SendTextInput({active_session, {{.kind = TextInputCommandKind::UpdateComposition, .text = "拼音"}}}).result_code == TextInputResultCode::Ok);
  ui.Pump();
  CHECK(source.Get().composition.has_value());
  CHECK(ui.ActiveTextInput()->session_id == active_session);
  ui.PressKey(Key::Enter, {.control = true});
  CHECK(ui.ActiveTextInput()->session_id == active_session);
  CHECK(ui.SendTextInput({active_session, {{.kind = TextInputCommandKind::FinishComposition}}}).result_code == TextInputResultCode::Ok);
  ui.Pump();
  input.ReplaceText("## split\n\nnew paragraph");
  CHECK(ui.ActiveTextInput()->session_id == active_session);
  ui.Find(UiSelector::Text("second😀")).Tap();
  ui.Pump();
  CHECK(input.One().value == "second😀"); // Frozen row maps past the resized/split block.
  input.ReplaceText("second changed");
  ui.Find(UiSelector::Key("outside")).Tap();
  ui.Pump();
  CHECK(ui.Find(UiSelector::Text("split")).Exists());
  CHECK(ui.Find(UiSelector::Text("new paragraph")).Exists());
  CHECK(ui.Find(UiSelector::Text("second changed")).Exists());
  CHECK(!ui.Find(UiSelector::Key("markdown-block-preview")).Exists());
  CHECK(source.Get().text == "# heading\n\n## split\n\nnew paragraph\n\nsecond changed\n\n- read only\n");
  CHECK(error.empty());
  source = TextEditingValue::FromText("[[page|label]]");
  ui.Pump();
  ui.Find(UiSelector::Text("label")).Tap();
  CHECK(input.One().value == "[[page|label]]"); // Rendered Wiki glyphs enter editing as well.
  ui.Find(UiSelector::Key("outside")).Tap();
  ui.Pump();
  source = TextEditingValue::FromText("");
  ui.Pump();
  auto empty = ui.Find(UiSelector::Key("markdown-empty-input"));
  empty.Tap();
  empty.EnterText("# start\n\nwritten");
  CHECK(ui.Find(UiSelector::Key("markdown-block-preview")).Find(UiSelector::Text("written")).Exists());
  ui.Find(UiSelector::Key("outside")).Tap();
  ui.Pump();
  CHECK(ui.Find(UiSelector::Text("start")).Exists());
  CHECK(ui.Find(UiSelector::Text("written")).Exists());
  std::string long_source;
  for (int i = 0; i < 200; ++i) long_source += "paragraph " + std::to_string(i) + "\n\n";
  source = TextEditingValue::FromText(long_source);
  ui.Pump();
  CHECK(!ui.Find(UiSelector::Text("paragraph 199")).Exists());
  CHECK(ui.Find(UiSelector::Type<TextField>()).Count() < 35);
  CHECK(scroll.ScrollToItem(398));
  ui.PumpAndSettle();
  ui.Find(UiSelector::Text("paragraph 199")).Tap();
  input.ReplaceText("last edited");
  CHECK(source.Get().text.ends_with("last edited\n\n"));
  const auto last_session = ui.ActiveTextInput()->session_id;
  CHECK(ui.SendTextInput({last_session, {{.kind = TextInputCommandKind::UpdateComposition, .text = "组合"}}}).result_code == TextInputResultCode::Ok);
  ui.Pump();
  CHECK(scroll.ScrollToItem(0));
  ui.PumpAndSettle();
  CHECK(!ui.Find(UiSelector::Text("paragraph 199")).Exists());
  CHECK(source.Get().text.ends_with("last edited组合\n\n"));
  CHECK(!source.Get().composition);
  CHECK(scroll.ScrollToItem(398));
  ui.PumpAndSettle();
  CHECK(ui.Find(UiSelector::Text("last edited组合")).Exists());
  CHECK(error.empty());
}); }
