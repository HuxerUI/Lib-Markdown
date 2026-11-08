#include <huxerui/huxerui.h>
#include <huxerui/markdown.h>
#include <huxerui/testing/ui_test.h>
#include "test.h"

using namespace huxerui;
using namespace huxerui::markdown;
using namespace huxerui::testing;

State<TextEditingValue> rich_source;
State<bool> custom_code;
ScrollController editor_scroll;
std::string rich_error;

View RichEditorRoot() {
  rich_source = UseState(TextEditingValue::FromText("# Heading\n\nplain **中文😀** *italic*\n\nlast\n"));
  custom_code = UseState(false);
  editor_scroll = UseScrollController();
  MarkdownOptions display;
  if (custom_code) display.code_block_renderer = [](const MarkdownNode&) -> View {
    return Text("custom code").Key("markdown-custom-code");
  };
  return ProvideEnvironment(TextSelectionMenuLabels{"Cut", "Copy", "Paste", "Select all"}, MaterialTheme(Column{
    Button("outside").Key("outside"),
    MarkdownEditor(rich_source, {.markdown_options = display,
        .virtualization = {.scroll_controller = editor_scroll}}).On<MarkdownEditorEvents::Changed>([](const TextEditingValue& next) { rich_source = next; })
        .On<MarkdownEditorEvents::Error>([](const std::string& error) { rich_error = error; }).With(Grow{}),
  }.With(CrossAlign(CrossAxisAlignment::Stretch))));
}

int main() { return Run([] {
  Application app(RichEditorRoot, {.show_debug_overlay = false});
  UiTest ui(app, {.viewport = {650, 600}});
  CHECK(ui.Find(UiSelector::Key("rich-text-editor")).Exists());
  CHECK(!ui.Find(UiSelector::Key("markdown-block-input")).Exists());
  ui.Find(UiSelector::Text("plain 中文😀 italic")).Tap();
  CHECK(ui.ActiveTextInput().has_value());
  const auto session = ui.ActiveTextInput()->session_id;
  const auto command = [&](TextInputCommand value) {
    CHECK(ui.SendTextInput({session, {std::move(value)}}).result_code == TextInputResultCode::Ok);
    ui.Pump();
  };
  command({.kind = TextInputCommandKind::SetSelection, .selection_after = TextSelection{18, 18}});
  command({.kind = TextInputCommandKind::CommitText, .text = "追加"});
  const std::string edited = "# Heading\n\nplain **中文😀追加** *italic*\n\nlast\n";
  CHECK(rich_source->text == edited);
  CHECK(rich_source->selection == (TextSelection{27, 27}));
  CHECK(ui.ActiveTextInput()->selection == (TextSelection{20, 20}));

  ui.PressKey(Key::Z, {.control = true});
  ui.Pump();
  CHECK(rich_source->text == "# Heading\n\nplain **中文😀** *italic*\n\nlast\n");
  CHECK(rich_source->selection == (TextSelection{25, 25}));
  CHECK(ui.ActiveTextInput()->selection == (TextSelection{18, 18}));
  CHECK(ui.ActiveTextInput()->session_id == session);
  ui.PressKey(Key::Y, {.control = true});
  ui.Pump();
  CHECK(rich_source->text == edited);
  CHECK(rich_source->selection == (TextSelection{27, 27}));

  command({.kind = TextInputCommandKind::UpdateComposition, .text = "拼音"});
  CHECK(rich_source->composition.has_value());
  CHECK(*rich_source->composition == (TextRange{25, 27}));
  CHECK(ui.ActiveTextInput()->session_id == session);
  command({.kind = TextInputCommandKind::CancelComposition});
  CHECK(rich_source->text == edited);
  CHECK(!rich_source->composition);
  CHECK(rich_error.empty());

  command({.kind = TextInputCommandKind::SetSelection, .selection_after = TextSelection{7, 7}});
  const auto before_enter = rich_source.Get();
  ui.PressKey(Key::Enter);
  ui.Pump();
  CHECK(rich_source->text != before_enter.text);
  CHECK(rich_error.empty());
  ui.PressKey(Key::Z, {.control = true});
  ui.Pump();
  CHECK(rich_source->text == before_enter.text);
  CHECK(rich_source->selection == before_enter.selection);
  CHECK(ui.ActiveTextInput()->session_id == session);
  CHECK(rich_error.empty());

  custom_code = true;
  rich_source = TextEditingValue::FromText("intro\n\n```cpp\nint x;\n```\n");
  ui.Pump();
  CHECK(ui.Find(UiSelector::Key("rich-text-editor")).Exists());
  CHECK(ui.Find(UiSelector::Key("markdown-custom-code")).Exists());

  custom_code = false;
  std::string paragraphs;
  for (int index = 0; index < 120; ++index)
    paragraphs += "Paragraph " + std::to_string(index) + "\n\n";
  rich_source = TextEditingValue::FromText(std::move(paragraphs));
  ui.Pump();
  CHECK(!ui.Find(UiSelector::Text("Paragraph 100")).Exists());
  CHECK(editor_scroll.ScrollToItem(100));
  ui.PumpAndSettle();
  CHECK(ui.Find(UiSelector::Text("Paragraph 100")).One().in_viewport);
  CHECK(rich_error.empty());
}); }
