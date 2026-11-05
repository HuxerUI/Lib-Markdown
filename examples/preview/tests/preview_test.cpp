#include "../src/app.h"
#include "../src/samples.h"
#include "../../../tests/test.h"
#include <huxerui/markdown.h>
#include <huxerui/testing/ui_test.h>

using namespace huxerui;
using namespace huxerui::testing;

class PreviewResources final : public PlatformResources {
public:
  explicit PreviewResources(std::string root) : root_(std::move(root)) {}
  ResourceConfiguration Configuration() const override { return {}; }
  std::optional<InputStream> OpenRead(std::string_view path) override {
    auto result = File(root_ + "/" + std::string(path)).OpenRead();
    if (!result.Succeeded()) return std::nullopt;
    return std::move(result).Value();
  }
private:
  std::string root_;
};

int main(int argc, char** argv) { return Run([&] {
  CHECK(argc == 2);
  for (const auto& sample : markdown_preview::Samples())
    CHECK(!markdown::ParseMarkdown(sample.source).Root().children.empty());

  UiTest ui(application, {.viewport = {1180, 820}, .resource_provider = std::make_shared<PreviewResources>(argv[1])});
  const auto preview = [&] { return ui.Find(UiSelector::Key("preview-scroll")); };
  CHECK(preview().Find(UiSelector::Text("你好，Markdown")).Exists());
  CHECK(ui.Find(UiSelector::Key("source-editor")).Exists());
  ui.Find(UiSelector::Key("sample-1")).Tap();
  CHECK(preview().Find(UiSelector::Text("列表与表格")).Exists());
  ui.Find(UiSelector::Key("sample-3")).Tap();
  CHECK(preview().Find(UiSelector::Key("custom-code")).Exists());
  CHECK(preview().Find(UiSelector::Key("custom-image")).Exists());
  ui.Find(UiSelector::Key("custom-toggle")).Tap();
  CHECK(!preview().Find(UiSelector::Key("custom-code")).Exists());
  CHECK(!preview().Find(UiSelector::Key("custom-image")).Exists());
  ui.Find(UiSelector::Key("sample-4")).Tap();
  CHECK(preview().Find(UiSelector::Text("第 20 节")).Exists());
  CHECK(preview().ScrollUntil(UiSelector::Text("第 20 节"), {.step = {0, 400}, .maximum_steps = 40}).One().in_viewport);
  ui.Find(UiSelector::Key("sample-0")).Tap();
  CHECK(preview().Find(UiSelector::Text("你好，Markdown")).One().in_viewport);
  auto editor = ui.Find(UiSelector::Key("source-editor"));
  editor.Tap();
  editor.ReplaceText("# 编辑后的标题\n\n[点我](../guide.md)");
  // Editing is explicit: the previous snapshot stays visible until Render.
  CHECK(preview().Find(UiSelector::Text("你好，Markdown")).Exists());
  ui.Find(UiSelector::Key("render")).Tap();
  CHECK(preview().Find(UiSelector::Text("编辑后的标题")).Exists());
  const auto link = preview().Find(UiSelector::Text("点我")).One().bounds;
  ui.TapAt({link.x + 4, link.y + link.height / 2});
  CHECK(ui.Find(UiSelector::Key("status")).One().text == "链接：../guide.md");
  ui.Find(UiSelector::Key("theme-toggle")).Tap();
  CHECK(ui.Find(UiSelector::Text("浅色主题")).Exists());
  CHECK(preview().Find(UiSelector::Text("编辑后的标题")).Exists());
  ui.Find(UiSelector::Key("source-toggle")).Tap();
  CHECK(!ui.Find(UiSelector::Key("source-editor")).Exists());
  ui.Find(UiSelector::Key("source-toggle")).Tap();
  CHECK(ui.Find(UiSelector::Key("source-editor")).One().value == "# 编辑后的标题\n\n[点我](../guide.md)");
  ui.Find(UiSelector::Key("reset")).Tap();
  CHECK(preview().Find(UiSelector::Text("你好，Markdown")).Exists());
  ui.SetWindowMetrics({.viewport = {480, 820}});
  ui.Pump();
  const auto source_bounds = ui.Find(UiSelector::Key("source-editor")).One().bounds;
  const auto preview_bounds = preview().One().bounds;
  CHECK(preview_bounds.y > source_bounds.y);
  CHECK(preview_bounds.x < 480 && preview_bounds.width <= 480);
}); }
