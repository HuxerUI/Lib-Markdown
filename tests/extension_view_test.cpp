#include <huxerui/huxerui.h>
#include <huxerui/markdown.h>
#include <huxerui/testing/ui_test.h>
#include "inline.h"
#include "test.h"

using namespace huxerui;
using namespace huxerui::markdown;
using namespace huxerui::testing;

std::function<View()> feature_root;
View FeatureRoot() { return feature_root(); }

int main() { return Run([] {
  const ParseOptions parse{.highlight = true, .insertions = true, .underline = true,
      .wiki_links = true, .admonitions = true, .footnotes = true};
  const auto styled = ParseMarkdown("==bright **bold**== ++added++ _under_ ~~gone~~", parse);
  const auto style = MarkdownStyle::FromTheme(MaterialDarkThemeSpec());
  const auto inline_text = markdown::detail::BuildInline(styled.Root().children[0], style);
  CHECK(inline_text.text.PlainText() == "bright bold added under gone");
  bool highlight = false, nested = false, underline = false, strike = false;
  for (const auto& span : inline_text.text.StyleRanges()) {
    highlight |= span.style.background == style.highlight_background;
    nested |= span.style.background == style.highlight_background && span.style.font_weight == FontWeight::Bold;
    underline |= span.style.decoration && HasTextDecoration(*span.style.decoration, TextDecoration::Underline);
    strike |= span.style.decoration && HasTextDecoration(*span.style.decoration, TextDecoration::StrikeThrough);
  }
  CHECK(highlight && nested && underline && strike);
  CHECK(style.highlight_foreground == MaterialDarkThemeSpec().colors.on_primary_container);
  const auto mixed = ParseMarkdown("[bad](javascript:x) [[javascript:Page|Wiki]] note[^中文]\n\n[^中文]: body", parse);
  const auto links = markdown::detail::BuildInline(mixed.Root().children[0], style, [](std::string_view) { return false; });
  CHECK(links.links.size() == 2);
  CHECK(links.links[0].kind == NodeKind::WikiLink);
  CHECK(links.links[0].destination == "javascript:Page");
  CHECK(links.links[1].kind == NodeKind::FootnoteReference);
  CHECK(links.links[1].footnote_label == "中文");

  const auto document = ParseMarkdown(
      "# Extensions\n\n==bright== ++added++ _under_\n\n[[javascript:Page|Wiki]]\n\nnote[^脚注]\n\n"
      "> [!WARNING]\n> alert **body**\n\n[^脚注]: footnote body\n", parse);
  for (const bool virtualized : {false, true}) {
    int wiki_calls = 0, note_calls = 0, url_calls = 0, last_version = 0;
    MarkdownWikiLinkActivation wiki;
    MarkdownFootnoteActivation note;
    std::shared_ptr<Clipboard> clipboard;
    State<int> version;
    State<bool> dark;
    State<bool> custom;
    State<bool> mounted;
    feature_root = [&]() -> View {
      clipboard = UseApplication().Clipboard();
      version = UseState(1);
      dark = UseState(false);
      custom = UseState(false);
      mounted = UseState(true);
      const int binding = version.Get();
      MarkdownOptions options;
      if (custom.Get()) options.admonition_renderer = [](const MarkdownNode& node) -> View {
        CHECK(node.admonition_kind == AdmonitionKind::Warning);
        return Text("custom: " + PlainText(node));
      };
      View content = Text("unmounted");
      if (mounted.Get()) {
        content = virtualized ? VirtualizedMarkdown(document, options) : Markdown(document, options);
        content = std::move(content)
            .On<MarkdownEvents::LinkActivated>([&](const MarkdownLinkActivation&) { ++url_calls; })
            .On<MarkdownEvents::WikiLinkActivated>([&, binding](const MarkdownWikiLinkActivation& event) {
              ++wiki_calls; wiki = event; last_version = binding;
            })
            .On<MarkdownEvents::FootnoteActivated>([&, binding](const MarkdownFootnoteActivation& event) {
              ++note_calls; note = event; last_version = binding;
            });
        if (!virtualized) content = ScrollView(std::move(content));
      }
      return MaterialTheme(dark.Get() ? MaterialDarkThemeSpec() : MaterialLightThemeSpec(),
          Column{std::move(content).With(Grow{})}.With(CrossAlign(CrossAxisAlignment::Stretch)));
    };
    Application app(FeatureRoot, {.show_debug_overlay = false});
    UiTest ui(app, {.viewport = {700, 1000}});
    const auto activate = [&](const std::string& label) {
      ui.FindSemantics(UiSemanticSelector::AllOf(UiSemanticSelector::Role(SemanticRole::Link),
          UiSemanticSelector::Label(label))).PerformSemanticAction({SemanticActionKind::Activate, {}});
    };
    CHECK(ui.Find(UiSelector::Text("bright added under")).Exists());
    CHECK(ui.Find(UiSelector::Text("Warning")).Exists());
    CHECK(ui.Find(UiSelector::Text("alert body")).Exists());
    CHECK(ui.Find(UiSelector::Text("footnote body")).Exists());
    activate("Wiki");
    CHECK(wiki_calls == 1 && note_calls == 0 && url_calls == 0);
    CHECK(wiki.target == "javascript:Page" && last_version == 1);
    activate("[1]");
    CHECK(wiki_calls == 1 && note_calls == 1 && url_calls == 0);
    CHECK(note.number == 1 && note.label == "脚注" && note.reference == 1);
    version = 2;
    dark = true;
    ui.PumpAndSettle();
    activate("Wiki");
    CHECK(wiki_calls == 2 && last_version == 2);
    activate("[1]");
    CHECK(note_calls == 2 && last_version == 2);
    const auto text = ui.Find(UiSelector::Text("bright added under")).One().bounds;
    ui.TapAt({text.x + 4, text.y + text.height / 2});
    ui.PressKey(Key::A, {.control = true});
    ui.PressKey(Key::C, {.control = true});
    const auto copied = clipboard->ReadText().value_or("");
    CHECK(copied.find("bright added under") != std::string::npos);
    CHECK(copied.find("note[1]") != std::string::npos);
    CHECK(copied.find("alert body") != std::string::npos);
    CHECK(copied.find("footnote body") != std::string::npos);
    custom = true;
    ui.PumpAndSettle();
    CHECK(ui.Find(UiSelector::Text("custom: alert body")).Exists());
    CHECK(!ui.Find(UiSelector::Text("Warning")).Exists());
    mounted = false;
    ui.PumpAndSettle();
    CHECK(!ui.FindSemantics(UiSemanticSelector::Role(SemanticRole::Link)).Exists());
  }
}); }
