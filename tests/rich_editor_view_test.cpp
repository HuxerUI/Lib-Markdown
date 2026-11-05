#include <huxerui/huxerui.h>
#include <huxerui/markdown.h>
#include <huxerui/testing/ui_test.h>
#include "test.h"
using namespace huxerui;
using namespace huxerui::markdown;
using namespace huxerui::testing;
State<TextEditingValue> rich_source;
std::string rich_error;
View RichEditorRoot() {
  rich_source = UseState(TextEditingValue::FromText("# Heading\n\nplain **中文😀** *italic*\n\nlast\n"));
  return ProvideEnvironment(TextSelectionMenuLabels{"Cut", "Copy", "Paste", "Select all"}, MaterialTheme(Column{
    Button("outside").Key("outside"),
    MarkdownEditor(rich_source).On<MarkdownEditorEvents::Changed>([](const TextEditingValue& next) { rich_source = next; })
        .On<MarkdownEditorEvents::Error>([](const std::string& error) { rich_error = error; }).With(Grow{}),
  }.With(CrossAlign(CrossAxisAlignment::Stretch))));
}
int main() { return Run([] {
  Application app(RichEditorRoot, {.show_debug_overlay = false});
  UiTest ui(app, {.viewport = {650, 600}});
  CHECK(ui.Find(UiSelector::Value("Heading")).Exists());
  ui.Find(UiSelector::Value("plain 中文😀 italic")).Tap();
  auto field = ui.Find(UiSelector::AllOf(UiSelector::Key("markdown-block-input"), UiSelector::Focused(true)));
  CHECK(field.One().value == "plain 中文😀 italic");
  CHECK(rich_source.Get().text == "# Heading\n\nplain **中文😀** *italic*\n\nlast\n");
  CHECK(!ui.Find(UiSelector::Key("markdown-block-preview")).Exists());
  const auto session = ui.ActiveTextInput()->session_id;
  field.SetSelection({10, 10});
  field.EnterText("追加");
  CHECK(rich_source.Get().text == "# Heading\n\nplain **中文😀追加** *italic*\n\nlast\n");
  field.SetSelection({0, AttributedText(*field.One().value).Length()});
  ui.PressKey(Key::Backspace);
  CHECK(field.One().value == "");
  ui.PressKey(Key::Z, {.control = true});
  CHECK(field.One().value == "plain 中文😀追加 italic");
  CHECK(rich_source.Get().text == "# Heading\n\nplain **中文😀追加** *italic*\n\nlast\n");
  CHECK(ui.ActiveTextInput()->session_id == session);
  CHECK(field.One().value == "plain 中文😀追加 italic");
  CHECK(ui.SendTextInput({session, {{.kind = TextInputCommandKind::UpdateComposition, .text = "拼音"}}}).result_code == TextInputResultCode::Ok);
  ui.Pump();
  CHECK(rich_source.Get().composition.has_value());
  CHECK(ui.ActiveTextInput()->session_id == session);
  CHECK(ui.SendTextInput({session, {{.kind = TextInputCommandKind::CancelComposition}}}).result_code == TextInputResultCode::Ok);
  ui.Pump();
  CHECK(field.One().value == "plain 中文😀追加 italic");
  CHECK(rich_source.Get().text == "# Heading\n\nplain **中文😀追加** *italic*\n\nlast\n");
  ui.Find(UiSelector::Key("outside")).Tap();
  ui.Pump();
  CHECK(ui.Find(UiSelector::Value("plain 中文😀追加 italic")).Exists());
  ui.Find(UiSelector::Value("Heading")).Tap();
  field.ReplaceText("标题修改😀");
  CHECK(rich_source.Get().text.starts_with("# 标题修改😀\n\n"));
  CHECK(!ui.Find(UiSelector::Key("markdown-block-preview")).Exists());
  CHECK(rich_error.empty());
  ui.PressKey(Key::Enter);
  ui.Pump();
  CHECK(!rich_error.empty());
  CHECK(ui.Find(UiSelector::Value("标题修改😀")).Exists());
  CHECK(!ui.Find(UiSelector::Value("标题修改😀\n")).Exists());
  CHECK(rich_source.Get().text.starts_with("# 标题修改😀\n\n"));
}); }
