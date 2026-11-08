#include <huxerui/huxerui.h>
#include <huxerui/markdown.h>
#include <huxerui/testing/ui_test.h>
#include "../src/discard.h"
#include "test.h"
#include <cmath>
#include <limits>

using namespace huxerui;
using namespace huxerui::markdown;
using namespace huxerui::testing;

std::function<View()> math_root;
View MathRoot() { return math_root(); }

int main() { return Run([] {
  auto invalid = MarkdownStyle{};
  invalid.blank_line_height = -1;
  Throws<std::invalid_argument>([&] { LIB_MARKDOWN_DISCARD(Markdown("x", {.style = invalid})); });
  invalid.blank_line_height = std::numeric_limits<float>::quiet_NaN();
  Throws<std::invalid_argument>([&] { LIB_MARKDOWN_DISCARD(VirtualizedMarkdown("x", {.style = invalid})); });
  const auto document = ParseMarkdown("before $x^2$ after\n\n\nnext\n\n$$\\frac{a}{b}$$\n\n$x$",
      {.math = true, .preserve_blank_lines = true});
  for (const bool virtualized : {false, true}) {
    State<bool> custom, dark;
    State<float> blank_height;
    std::shared_ptr<Clipboard> clipboard;
    int display_calls = 0, inline_calls = 0;
    math_root = [&]() -> View {
      custom = UseState(false);
      dark = UseState(false);
      blank_height = UseState(18.0F);
      clipboard = UseApplication().Clipboard();
      return ProvideEnvironment(TextSelectionMenuLabels{"Cut", "Copy", "Paste", "Select all"},
          MaterialTheme(dark ? MaterialDarkThemeSpec() : MaterialLightThemeSpec(), Scope([&]() -> View {
        auto style = MarkdownStyle::FromTheme(UseTheme());
        style.blank_line_height = static_cast<float>(blank_height);
        MarkdownOptions options{.style = style};
        if (custom) options.standalone_math_renderer = [&](const MarkdownNode& math) -> View {
          CHECK(math.children.empty());
          if (math.kind == NodeKind::DisplayMath) {
            ++display_calls;
            CHECK(math.text == "\\frac{a}{b}");
            return Text("display: " + math.text);
          }
          CHECK(math.kind == NodeKind::InlineMath && math.text == "x");
          ++inline_calls;
          return Text("inline: " + math.text);
        };
        if (virtualized) return VirtualizedMarkdown(document, options, {.cache_extent = 1000});
        return ScrollView(Markdown(document, options));
      })));
    };
    Application app(MathRoot, {.show_debug_overlay = false});
    UiTest ui(app, {.viewport = {600, 800}});
    CHECK(ui.Find(UiSelector::Text("before x^2 after")).Exists());
    CHECK(ui.Find(UiSelector::Text("\\frac{a}{b}")).Exists());
    const auto first = ui.Find(UiSelector::Text("before x^2 after")).One().bounds;
    const auto next = ui.Find(UiSelector::Text("next")).One().bounds;
    CHECK(std::abs(next.y - first.y - first.height - 36) < 1);
    ui.TapAt({first.x + 4, first.y + first.height / 2});
    ui.PressKey(Key::A, {.control = true});
    ui.PressKey(Key::C, {.control = true});
    CHECK(clipboard->ReadText() == "before x^2 after\n\n\nnext\n\n\\frac{a}{b}\n\nx");
    custom = true;
    dark = true;
    ui.PumpAndSettle();
    CHECK(ui.Find(UiSelector::Text("display: \\frac{a}{b}")).Exists());
    CHECK(ui.Find(UiSelector::Text("inline: x")).Exists());
    CHECK(ui.Find(UiSelector::Text("before x^2 after")).Exists()); // Mixed inline does not call custom View renderer.
    CHECK(display_calls > 0 && inline_calls > 0);
    ui.TapAt({first.x + 4, first.y + first.height / 2});
    ui.PressKey(Key::A, {.control = true});
    ui.PressKey(Key::C, {.control = true});
    CHECK(clipboard->ReadText() == "before x^2 after\n\n\nnext\n\n\\frac{a}{b}\n\nx");
    blank_height = 0;
    ui.PumpAndSettle();
    const auto collapsed_first = ui.Find(UiSelector::Text("before x^2 after")).One().bounds;
    const auto collapsed_next = ui.Find(UiSelector::Text("next")).One().bounds;
    CHECK(std::abs(collapsed_next.y - collapsed_first.y - collapsed_first.height) < 1);
  }

  math_root = []() -> View {
    return Markdown(ParseMarkdown("a\n\n\nb", {.preserve_blank_lines = true}), {.selectable = false});
  };
  Application simple(MathRoot, {.show_debug_overlay = false});
  UiTest ui(simple);
  CHECK(ui.Find(UiSelector::Text("a")).Exists());
  CHECK(ui.Find(UiSelector::Text("b")).Exists());
}); }
