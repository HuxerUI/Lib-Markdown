#include <huxerui/huxerui.h>
#include <huxerui/markdown.h>
#include <huxerui/testing/ui_test.h>

#include "test.h"

using namespace huxerui;
using namespace huxerui::markdown;
using namespace huxerui::testing;

struct Activation {
  int owner;
  int handler_version;
  std::string destination;
  bool operator==(const Activation&) const = default;
};

std::vector<Activation> activations;
EventEmitter retained_emitter;
const MarkdownNode* observed_code = nullptr;
Color observed_foreground;
std::shared_ptr<Clipboard> clipboard;

View EventApp() {
  clipboard = UseApplication().Clipboard();
  static const auto document = ParseMarkdown(
      "[Web](https://example.com/?a=1&amp;b=2)\n\n"
      "[Relative](../guide.md)\n\n[Unicode](../文档/说明.md)\n\n[Anchor](#details)\n\n"
      "[**Bold** *Italic*](../nested.md)\n\n[One](../one)[Two](../two)\n\n"
      "[Blocked](javascript:alert%281%29)\n\n```cpp\nretained\n```");
  auto mounted = UseState(true);
  auto version = UseState(0);
  auto dark = UseState(false);
  return Column{
    Row{
      Button("Replace handler").OnClick([version]() mutable { ++version; }),
      Button("Unmount").OnClick([mounted]() mutable { mounted = false; }),
      Button("Theme").OnClick([dark]() mutable { dark = !dark; }),
    },
    MaterialTheme(dark ? MaterialDarkThemeSpec() : MaterialLightThemeSpec(), Scope([=]() -> View {
      const int current_version = version;
      View first = Text("Detached");
      if (mounted) {
        first = Markdown(document, {
          .code_block_renderer = [](const MarkdownNode& code) -> View {
            retained_emitter = UseEvents();
            observed_code = &code;
            observed_foreground = UseTheme().colors.on_surface;
            return Text(code.text);
          },
        }).On<MarkdownEvents::LinkActivated>([current_version](const MarkdownLinkActivation& event) {
          activations.push_back({1, current_version, event.destination});
        });
      }
      return Column{
        std::move(first).Key("first"),
        Markdown("[Second](../guide.md)")
            .On<MarkdownEvents::LinkActivated>([](const MarkdownLinkActivation& event) {
              activations.push_back({2, 0, event.destination});
            }),
        Markdown("[Custom](app:article)", {.link_policy = [](std::string_view) { return true; }})
            .On<MarkdownEvents::LinkActivated>([](const MarkdownLinkActivation& event) {
              activations.push_back({3, 0, event.destination});
            }),
        Markdown("[Unhandled](https://example.com)", {.selectable = false}),
        Markdown(ParseMarkdown("soft\nbreak", {.soft_break_mode = SoftBreakMode::LineBreak})),
        Markdown(MarkdownDocument{}),
        Markdown("Copy **first**\n\nCopy *second*").Key("selection"),
      }.With(Spacing(4.0F), CrossAlign(CrossAxisAlignment::Stretch));
    })),
  }.With(Spacing(8.0F), CrossAlign(CrossAxisAlignment::Stretch));
}

int main() { return Run([] {
  Application app(EventApp, {.show_debug_overlay = false});
  UiTest ui(app, {.viewport = {900, 1000}});
  const auto activate = [&](const std::string& label) {
    ui.FindSemantics(UiSemanticSelector::AllOf(UiSemanticSelector::Role(SemanticRole::Link),
        UiSemanticSelector::Label(label))).PerformSemanticAction({SemanticActionKind::Activate, {}});
  };
  const auto expect = [&](int owner, int version, const std::string& destination, const auto& action) {
    const auto count = activations.size();
    action();
    CHECK(activations.size() == count + 1);
    CHECK(activations.back() == (Activation{owner, version, destination}));
  };

  const auto bounds = ui.Find(UiSelector::Text("Relative")).One().bounds;
  expect(1, 0, "../guide.md", [&] { ui.TapAt({bounds.x + 4, bounds.y + bounds.height / 2}); });
  for (const auto& [label, destination] : std::vector<std::pair<std::string, std::string>>{
      {"Web", "https://example.com/?a=1&b=2"}, {"Unicode", "../文档/说明.md"},
      {"Anchor", "#details"}, {"Bold Italic", "../nested.md"}, {"One", "../one"}, {"Two", "../two"}})
    expect(1, 0, destination, [&] { activate(label); });
  expect(2, 0, "../guide.md", [&] { activate("Second"); });
  expect(3, 0, "app:article", [&] { activate("Custom"); });

  const auto before_unhandled = activations.size();
  activate("Unhandled");
  CHECK(activations.size() == before_unhandled);
  CHECK(!ui.FindSemantics(UiSemanticSelector::AllOf(UiSemanticSelector::Role(SemanticRole::Link),
      UiSemanticSelector::Label("Blocked"))).Exists());
  const auto blocked = ui.Find(UiSelector::Text("Blocked")).One().bounds;
  ui.TapAt({blocked.x + 4, blocked.y + blocked.height / 2});
  CHECK(activations.size() == before_unhandled);
  CHECK(ui.Find(UiSelector::Text("soft\nbreak")).Exists());
  const auto selectable = ui.Find(UiSelector::Text("Copy first")).One().bounds;
  ui.TapAt({selectable.x + 4, selectable.y + selectable.height / 2});
  ui.PressKey(Key::A, {.control = true});
  ui.PressKey(Key::C, {.control = true});
  CHECK(clipboard->ReadText() == "Copy first\nCopy second");

  auto previous_emitter = retained_emitter;
  const auto* previous_code = observed_code;
  CHECK(previous_emitter.IsConnected());
  ui.Find(UiSelector::Text("Replace handler")).Tap();
  CHECK(previous_emitter.IsConnected());
  expect(1, 1, "../guide.md", [&] { activate("Relative"); });
  expect(1, 1, "retained", [&] {
    previous_emitter.Emit<MarkdownEvents::LinkActivated>(MarkdownLinkActivation{"retained"});
  });
  const auto previous_foreground = observed_foreground;
  ui.Find(UiSelector::Text("Theme")).Tap();
  CHECK(observed_code == previous_code);
  CHECK(observed_foreground != previous_foreground);
  expect(1, 1, "#details", [&] { activate("Anchor"); });

  ui.Find(UiSelector::Text("Unmount")).Tap();
  CHECK(ui.Find(UiSelector::Text("Detached")).Exists());
  CHECK(!previous_emitter.IsConnected());
  const auto count = activations.size();
  previous_emitter.Emit<MarkdownEvents::LinkActivated>(MarkdownLinkActivation{"after-unmount"});
  CHECK(activations.size() == count);
  expect(2, 0, "../guide.md", [&] { activate("Second"); });
}); }
