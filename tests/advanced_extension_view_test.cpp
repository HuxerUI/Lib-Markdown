#include <huxerui/huxerui.h>
#include <huxerui/markdown.h>
#include <huxerui/testing/ui_test.h>
#include "inline.h"
#include "test.h"
using namespace huxerui;
using namespace huxerui::markdown;
using namespace huxerui::testing;
bool virtual_spoilers = false;
std::shared_ptr<Clipboard> spoiler_clipboard;
std::string activated;
View SpoilerRoot() {
  auto document = ParseMarkdown("before ||hidden **bold** [link](../target)|| and ||other|| after", {.spoilers = true});
  spoiler_clipboard = UseApplication().Clipboard();
  View content = virtual_spoilers ? VirtualizedMarkdown(document) : Markdown(document);
  content = std::move(content).On<MarkdownEvents::LinkActivated>([](const MarkdownLinkActivation& event) { activated = event.destination; });
  if (!virtual_spoilers) content = ScrollView(std::move(content));
  return ProvideEnvironment(TextSelectionMenuLabels{"Cut", "Copy", "Paste", "Select all"},
      MaterialTheme(std::move(content)));
}
int main() { return Run([] {
  const auto doc = ParseMarkdown("before ||hidden **bold** [link](../target)|| after", {.spoilers = true});
  auto style = MarkdownStyle{};
  auto masked = huxerui::markdown::detail::BuildInline(doc.Root().children.front(), style);
  CHECK(masked.links.size() == 1 && masked.links.front().kind == NodeKind::Spoiler);
  bool found_mask = false;
  for (const auto& range : masked.text.StyleRanges()) {
    if (range.style.background == style.spoiler_cover) {
      CHECK(range.style.foreground == style.spoiler_cover);
      found_mask = true;
    }
  }
  CHECK(found_mask);
  const std::set<unsigned> shown{1};
  const auto visible = huxerui::markdown::detail::BuildInline(doc.Root().children.front(), style, {}, &shown);
  CHECK(visible.links.size() == 1 && visible.links.front().kind == NodeKind::Link);
  for (const bool virtualized : {false, true}) {
    virtual_spoilers = virtualized;
    activated.clear();
    Application app(SpoilerRoot, {.show_debug_overlay = false});
    UiTest ui(app, {.viewport = {650, 450}});
    auto hidden = ui.FindSemantics(UiSemanticSelector::AllOf(UiSemanticSelector::Role(SemanticRole::Link), UiSemanticSelector::Label("hidden bold link")));
    CHECK(hidden.Exists());
    hidden.PerformSemanticAction({SemanticActionKind::Activate, {}});
    CHECK(!hidden.Exists());
    CHECK(activated.empty());
    CHECK(ui.FindSemantics(UiSemanticSelector::AllOf(UiSemanticSelector::Role(SemanticRole::Link), UiSemanticSelector::Label("other"))).Exists());
    ui.FindSemantics(UiSemanticSelector::AllOf(UiSemanticSelector::Role(SemanticRole::Link), UiSemanticSelector::Label("link")))
        .PerformSemanticAction({SemanticActionKind::Activate, {}});
    CHECK(activated == "../target");
    ui.Find(UiSelector::Text("before hidden bold link and other after")).Tap();
    ui.PressKey(Key::A, {.control = true});
    ui.PressKey(Key::C, {.control = true});
    CHECK(spoiler_clipboard->ReadText() == "before hidden bold link and other after");
  }
}); }
