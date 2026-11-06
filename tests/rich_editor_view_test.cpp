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
  rich_error.clear();
  rich_source = TextEditingValue::FromText("3) first **bold**\r\n   - [x] child\r\n4) second *italic*\r\n\r\n"
      "> a **quote**\r\n\r\n| A | B |\r\n| --- | ---: |\r\n| **cell** |  |\r\n\r\n```cpp\r\nint x;\r\n```\r\n");
  ui.Pump();
  ui.Find(UiSelector::Value("first bold")).Tap();
  const auto list_session = ui.ActiveTextInput()->session_id;
  field.SetSelection({10, 10});
  field.EnterText("中文😀");
  CHECK(ui.ActiveTextInput()->session_id == list_session);
  CHECK(rich_source.Get().text.starts_with("3) first **bold中文😀**\r\n   - [x] child\r\n4) second *italic*\r\n"));
  CHECK(ui.Find(UiSelector::Text("3.")).Exists());
  ui.Find(UiSelector::Value("child")).Tap();
  CHECK(field.One().value == "child");
  field.ReplaceText("嵌套任务");
  CHECK(rich_source.Get().text.find("   - [x] 嵌套任务\r\n") != std::string::npos);
  ui.Find(UiSelector::Value("second italic")).Tap();
  CHECK(field.One().value == "second italic");
  field.SetSelection({13, 13});
  field.EnterText("!");
  CHECK(rich_source.Get().text.find("4) second *italic\\!*\r\n") != std::string::npos);
  CHECK(!ui.Find(UiSelector::Key("markdown-block-preview")).Exists());
  ui.Find(UiSelector::Value("a quote")).Tap();
  field.SetSelection({7, 7});
  field.EnterText(" edited");
  CHECK(rich_source.Get().text.find("> a **quote edited**\r\n") != std::string::npos);
  ui.Find(UiSelector::Value("cell")).Tap();
  field.SetSelection({4, 4});
  field.EnterText("中|");
  CHECK(rich_source.Get().text.find("| **cell中\\|** |  |\r\n") != std::string::npos);
  auto empty_cell = ui.Find(UiSelector::AllOf(UiSelector::Key("markdown-block-input"), UiSelector::Value("")));
  CHECK(empty_cell.Exists());
  empty_cell.Tap();
  CHECK(field.One().value == "");
  field.EnterText("空格😀");
  CHECK(rich_source.Get().text.find("| **cell中\\|** |  空格😀|\r\n") != std::string::npos);
  ui.PressKey(Key::Z, {.control = true});
  CHECK(field.One().value == "");
  ui.PressKey(Key::Y, {.control = true});
  CHECK(field.One().value == "空格😀");
  ui.PressKey(Key::Enter, {.control = true});
  CHECK(!field.Exists());
  ui.Find(UiSelector::Text("int x;\n")).Tap();
  CHECK(field.One().value == "```cpp\r\nint x;\r\n```");
  field.ReplaceText("```cpp\r\nint y;\r\n```");
  CHECK(rich_source.Get().text.ends_with("```cpp\r\nint y;\r\n```\r\n"));
  CHECK(rich_error.empty());
  rich_source = TextEditingValue::FromText("> first line\n> second line\n\nafter\n");
  ui.Pump();
  ui.Find(UiSelector::Value("first line second line")).Tap();
  field.ReplaceText("combined quote");
  CHECK(rich_source.Get().text == "> combined quote\n\nafter\n");
  rich_source = TextEditingValue::FromText("> first line  \n> second line\n\nafter\n");
  ui.Pump();
  ui.Find(UiSelector::Text("first line\nsecond line")).Tap();
  CHECK(field.One().value == "> first line  \n> second line");
  field.ReplaceText("> first line  \n> changed line");
  CHECK(rich_source.Get().text == "> first line  \n> changed line\n\nafter\n");
  CHECK(ui.Find(UiSelector::Key("markdown-block-preview")).Find(UiSelector::Text("first line\nchanged line")).Exists());
  CHECK(rich_error.empty());
}); }
