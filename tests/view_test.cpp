#include <huxerui/markdown.h>
#include <huxerui/huxerui.h>
#include "../src/discard.h"
#include <algorithm>
#include <limits>
#include "inline.h"
#include "test.h"
#ifdef MARKDOWN_HAS_UI_TESTING
#include <huxerui/testing/ui_test.h>
#endif

using namespace huxerui;
using namespace huxerui::markdown;

void CheckEditableInlineStyles(std::string_view source, ParseOptions options, const MarkdownStyle& theme,
                               const MarkdownLinkPolicy& policy = {}) {
  const auto document = ParseMarkdown(source, options);
  const auto& paragraph = document.Root().children.front();
  const auto preview = markdown::detail::BuildInline(paragraph, theme, policy).text;
  const MarkdownRichText editor(paragraph, options);
  const auto editable = AttributedText::FromRanges(editor.Value().text,
      markdown::detail::BuildEditableStyles(editor, theme.body, theme, policy));
  CHECK(editable.PlainText() == preview.PlainText());

  // Fill the preview's implicit base ranges to compare with the field's explicit styles.
  const TextSpanStyle base{.font = theme.body.font, .foreground = theme.body.foreground};
  std::vector<TextStyleRange> resolved;
  TextOffset offset = 0;
  for (auto range : preview.StyleRanges()) {
    if (offset < range.range.start) resolved.push_back({{offset, range.range.start}, base});
    if (!range.style.font) range.style.font = theme.body.font;
    if (!range.style.foreground) range.style.foreground = theme.body.foreground;
    offset = range.range.end;
    resolved.push_back(std::move(range));
  }
  if (offset < preview.Length()) resolved.push_back({{offset, preview.Length()}, base});
  const auto expected = preview.WithStyles(std::move(resolved));
  CHECK(std::ranges::equal(editable.StyleRanges(), expected.StyleRanges()));
}

#ifdef MARKDOWN_HAS_UI_TESTING
std::string activated;
#endif

int main() { return Run([] {
  const auto theme = MarkdownStyle::FromTheme(MaterialDarkThemeSpec());
  CHECK(theme.body.foreground == MaterialDarkThemeSpec().colors.on_surface);
  const std::string_view formatted = "普通 ***中文😀*** ~~gone~~ _under_ ++added++ ==mark== `code` "
      "[**guide**](../guide.md) [[Page|wiki]] [blocked](javascript:alert%281%29)";
  const ParseOptions formats{.highlight = true, .insertions = true, .underline = true, .wiki_links = true};
  CheckEditableInlineStyles(formatted, formats, theme);
  CheckEditableInlineStyles(formatted, formats, theme, [](std::string_view) { return true; });
  const auto doc = ParseMarkdown("\xF0\x9F\x98\x80 [**bold** *italic*](../guide.md)[next](../guide.md) ~~gone~~ `code`");
  const auto value = markdown::detail::BuildInline(doc.Root().children[0], theme);
  CHECK(value.text.PlainText() == "\xF0\x9F\x98\x80 bold italicnext gone code");
  CHECK(value.text.Length() == 28);
  CHECK(value.links.size() == 2);
  CHECK(value.links[0].range == (TextRange{3, 14}));
  CHECK(value.links[1].range == (TextRange{14, 18}));
  CHECK(value.links[0].destination == "../guide.md");
  CHECK(value.text.LinkRanges().size() == 2);
  CHECK(value.text.TextInRange(value.links[0].range) == "bold italic");
  bool bold = false, italic = false, strike = false, code = false;
  for (const auto& range : value.text.StyleRanges()) {
    if (range.style.font_weight == FontWeight::Bold) {
      bold = true;
      CHECK(range.style.foreground == theme.link);
      CHECK(HasTextDecoration(*range.style.decoration, TextDecoration::Underline));
    }
    italic |= range.style.font_slant == FontSlant::Italic;
    strike |= range.style.decoration && HasTextDecoration(*range.style.decoration, TextDecoration::StrikeThrough);
    code |= range.style.background == theme.code_background;
  }
  CHECK(bold && italic && strike && code);
  const auto unsafe = ParseMarkdown("[x](javascript:alert%281%29) [ok](https://example.com)");
  const auto filtered = markdown::detail::BuildInline(unsafe.Root(), theme);
  CHECK(filtered.links.size() == 1);
  CHECK(filtered.links[0].destination == "https://example.com");
  const auto custom = markdown::detail::BuildInline(unsafe.Root(), theme, [](std::string_view) { return true; });
  CHECK(custom.links.size() == 2);
  const auto nested = markdown::detail::BuildInline(ParseMarkdown("***both***").Root(), theme);
  CHECK(nested.text.StyleRanges()[0].style.font_weight == FontWeight::Bold);
  CHECK(nested.text.StyleRanges()[0].style.font_slant == FontSlant::Italic);
  const auto image_link = markdown::detail::BuildInline(ParseMarkdown("[![alt [inside](https://inner.example)](image.png)](https://outer.example)").Root(), theme);
  CHECK(image_link.text.PlainText() == ParseMarkdown("[![alt [inside](https://inner.example)](image.png)](https://outer.example)").PlainText());
  CHECK(image_link.links.size() == 1);
  CHECK(image_link.links[0].destination == "https://outer.example");

  LIB_MARKDOWN_DISCARD(Markdown(MarkdownDocument{}));
  auto invalid = MarkdownStyle{};
  invalid.table_cell_width = 0;
  Throws<std::invalid_argument>([&] { LIB_MARKDOWN_DISCARD(Markdown("x", {.style = invalid})); });
  invalid = MarkdownStyle{};
  invalid.heading_sizes[0] = std::numeric_limits<float>::quiet_NaN();
  Throws<std::invalid_argument>([&] { LIB_MARKDOWN_DISCARD(Markdown("x", {.style = invalid})); });

#ifdef MARKDOWN_HAS_UI_TESTING
  using namespace huxerui::testing;
  Application app([]() -> View {
    auto swapped = UseState(false);
    return Column{
      Button("Swap").OnClick([swapped]() mutable { swapped = !swapped; }),
      ScrollView(Markdown(swapped ? "# Updated" :
          "# Native\n\n[Go](../guide.md)\n\n- **first**\n  - nested\n\n> quote\n\n```cpp\nint n;\n```\n\n| A | B |\n|---|---|\n| long text wraps inside a fixed column | cell |\n\n![alt](image.png)",
          {}).On<MarkdownEvents::LinkActivated>([](const MarkdownLinkActivation& event) { activated = event.destination; })).With(Grow{}),
    }.With(CrossAlign(CrossAxisAlignment::Stretch));
  }, {.window = {.initial_size = {640, 900}}, .show_debug_overlay = false});
  UiTest ui(app);
  CHECK(ui.Find(UiSelector::Text("Native")).Exists());
  CHECK(ui.Find(UiSelector::Text("first")).Exists());
  CHECK(ui.Find(UiSelector::Text("nested")).Exists());
  CHECK(ui.Find(UiSelector::Text("alt")).Exists());
  CHECK(ui.Find(UiSelector::Text("int n;\n")).Exists());
  CHECK(ui.Find(UiSelector::Text("A")).One().bounds.width == ui.Find(UiSelector::Text("B")).One().bounds.width);
  const auto link_bounds = ui.Find(UiSelector::Text("Go")).One().bounds;
  ui.TapAt({link_bounds.x + 4.0F, link_bounds.y + link_bounds.height / 2.0F});
  CHECK(activated == "../guide.md");
  ui.Find(UiSelector::Text("Swap")).Tap();
  CHECK(ui.Find(UiSelector::Text("Updated")).Exists());
  CHECK(!ui.Find(UiSelector::Text("Native")).Exists());
  ui.PumpAndSettle();
#endif
}); }
