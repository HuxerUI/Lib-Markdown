#include <huxerui/huxerui.h>
#include <huxerui/markdown.h>
#include <huxerui/testing/ui_test.h>
#include "test.h"
#include <cmath>
#include <limits>

using namespace huxerui;
using namespace huxerui::markdown;
using namespace huxerui::testing;

State<MarkdownDocument> current;
State<bool> dark;
ScrollController scroll;
std::shared_ptr<Clipboard> clipboard;
int rendered = 0;
std::string destination;
const MarkdownNode* first_code = nullptr;
MarkdownDocument initial;

View VirtualApp() {
  current = UseState(initial);
  dark = UseState(false);
  clipboard = UseApplication().Clipboard();
  return MaterialTheme(dark.Get() ? MaterialDarkThemeSpec() : MaterialLightThemeSpec(),
      VirtualizedMarkdown(current.Get(), {
        .code_block_renderer = [](const MarkdownNode& node) -> View {
          ++rendered;
          if (node.text == "code 0\n") first_code = &node;
          return Text("custom " + node.text);
        },
      }, {.estimated_block_extent = 50, .cache_extent = 100, .scroll_controller = scroll})
      .On<MarkdownEvents::LinkActivated>([](const MarkdownLinkActivation& event) { destination = event.destination; })
      .Key("virtual"));
}

int main() { return Run([] {
  Throws<std::invalid_argument>([] { (void)VirtualizedMarkdown("x", {}, {.estimated_block_extent = 0}); });
  Throws<std::invalid_argument>([] { (void)VirtualizedMarkdown("x", {}, {.cache_extent = -1}); });
  Throws<std::invalid_argument>([] { (void)VirtualizedMarkdown("x", {}, {.cache_extent = std::numeric_limits<float>::infinity()}); });
  std::string source, expected;
  for (int i = 0; i < 300; ++i) {
    const auto number = std::to_string(i);
    source += "# record " + number + "\n\nbody " + number + " [link " + number + "](../" + number + ")\n\n```\ncode " + number + "\n```\n\n";
    if (i) expected += '\n';
    expected += "record " + number + "\nbody " + number + " link " + number + "\ncode " + number + "\n";
  }
  initial = ParseMarkdown(source);
  Application app(VirtualApp, {.show_debug_overlay = false});
  UiTest ui(app, {.viewport = {600, 420}});
  CHECK(scroll.IsConnected());
  CHECK(ui.Find(UiSelector::Text("record 0")).Exists());
  CHECK(!ui.Find(UiSelector::Text("record 299")).Exists());
  CHECK(ui.Find(UiSelector::Type<Text>()).Count() < 60);
  CHECK(rendered > 0 && rendered < 20);
  const auto* original_code = first_code;
  const auto start = ui.Find(UiSelector::Text("record 0")).One().bounds;
  ui.TapAt({start.x + 4, start.y + start.height / 2});
  ui.PressKey(Key::A, {.control = true});
  const auto before_copy = rendered;
  ui.PressKey(Key::C, {.control = true});
  CHECK(clipboard->ReadText() == expected);
  CHECK(rendered == before_copy);
  CHECK(!ui.Find(UiSelector::Text("record 299")).Exists());
  CHECK(scroll.ScrollToItem(450));
  ui.PumpAndSettle();
  CHECK(ui.Find(UiSelector::Text("record 150")).One().in_viewport);
  CHECK(!ui.Find(UiSelector::Text("record 0")).Exists());
  ui.PressKey(Key::C, {.control = true});
  CHECK(clipboard->ReadText() == expected);
  ui.FindSemantics(UiSemanticSelector::AllOf(UiSemanticSelector::Role(SemanticRole::Link),
      UiSemanticSelector::Label("link 150"))).PerformSemanticAction({SemanticActionKind::Activate, {}});
  CHECK(destination == "../150");
  const auto y = ui.Find(UiSelector::Text("record 150")).One().bounds.y;
  current = ParseMarkdown(source + "\n# appended\n");
  ui.PumpAndSettle();
  CHECK(std::abs(ui.Find(UiSelector::Text("record 150")).One().bounds.y - y) < 1);
  ui.PressKey(Key::C, {.control = true});
  CHECK(clipboard->ReadText() == expected); // Existing endpoints do not absorb appended text.
  ui.PressKey(Key::A, {.control = true});
  ui.PressKey(Key::C, {.control = true});
  CHECK(clipboard->ReadText() == expected + "\nappended");
  current = ParseMarkdown("# inserted before\n\n" + source + "\n# appended\n");
  ui.PumpAndSettle();
  CHECK(ui.Find(UiSelector::Text("record 150")).Exists());
  ui.PressKey(Key::C, {.control = true});
  CHECK(clipboard->ReadText() == expected + "\nappended");
  dark = true;
  ui.PumpAndSettle();
  CHECK(ui.Find(UiSelector::Text("record 150")).One().in_viewport);
  CHECK(scroll.ScrollTo(0));
  ui.PumpAndSettle();
  CHECK(first_code == original_code);
  CHECK(ui.Find(UiSelector::Text("inserted before")).One().in_viewport);
  ui.SetWindowMetrics({.viewport = {320, 420}});
  ui.PumpAndSettle();
  CHECK(ui.Find(UiSelector::Type<Text>()).Count() < 60);
  CHECK(scroll.ScrollToItem(901));
  ui.PumpAndSettle();
  CHECK(ui.Find(UiSelector::Text("appended")).One().in_viewport);
  current = MarkdownDocument{};
  ui.PumpAndSettle();
  CHECK(!ui.Find(UiSelector::Text("appended")).Exists());
  CHECK(scroll.Offset() == 0);

  // Exercise every renderer branch with logical selection bindings, including
  // nested containers, inline-only images and table cells.
  Application mixed([]() -> View {
    return VirtualizedMarkdown("# H\n\n> quote\n\n- one\n  - nested\n\n---\n\n| a | b |\n|---|---|\n| c | d |\n\n![alt](x)\n\n```\ncode\n```\n",
        {}, {.estimated_block_extent = 30, .cache_extent = 1000});
  }, {.show_debug_overlay = false});
  UiTest mixed_ui(mixed, {.viewport = {600, 1000}});
  CHECK(mixed_ui.Find(UiSelector::Text("nested")).Exists());
  CHECK(mixed_ui.Find(UiSelector::Text("alt")).Exists());
  CHECK(mixed_ui.Find(UiSelector::Text("c")).Exists());
  CHECK(mixed_ui.Find(UiSelector::Text("code\n")).Exists());

  Application no_selection([]() -> View {
    return VirtualizedMarkdown("[blocked](javascript:x)\n\n![picture](demo:x)\n\n```\ncode\n```\n",
        {.selectable = false, .standalone_image_renderer = [](const MarkdownNode&) -> View { return Text("custom image"); }});
  }, {.show_debug_overlay = false});
  UiTest simple(no_selection, {.viewport = {600, 600}});
  CHECK(simple.Find(UiSelector::Text("custom image")).Exists());
  CHECK(simple.Find(UiSelector::Text("code\n")).Exists());
  CHECK(!simple.FindSemantics(UiSemanticSelector::Role(SemanticRole::Link)).Exists());
}); }
