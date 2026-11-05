#include "../src/app.h"
#include "../src/samples.h"
#include "../../../tests/test.h"
#include <huxerui/markdown.h>
#include <huxerui/testing/ui_test.h>
#include <chrono>

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
  const auto advance = [&](std::chrono::milliseconds duration) {
    ui.Pump(duration);
    ui.Pump(); // Deliver the continuation queued by an expired virtual timer.
  };
  const auto preview = [&] { return ui.Find(UiSelector::Key("preview-scroll")); };
  const auto replace = [&](std::string text) {
    auto source = ui.Find(UiSelector::Key("source-editor"));
    source.Tap();
    source.ReplaceText(std::move(text));
  };
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
  // Typing alone publishes a new snapshot after the coalescing window.
  CHECK(preview().Find(UiSelector::Text("你好，Markdown")).Exists());
  advance(std::chrono::milliseconds(121));
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
  replace("# 过期输入\n");
  advance(std::chrono::milliseconds(60));
  replace("# 最新输入\n");
  advance(std::chrono::milliseconds(61));
  CHECK(preview().Find(UiSelector::Text("最新输入")).Exists());
  CHECK(!preview().Find(UiSelector::Text("过期输入")).Exists());
  advance(std::chrono::milliseconds(60));
  CHECK(preview().Find(UiSelector::Text("最新输入")).Exists());
  const auto unchanged_status = ui.Find(UiSelector::Key("status")).One().text;
  ui.Find(UiSelector::Key("source-editor")).SetSelection({0, 0});
  advance(std::chrono::milliseconds(200));
  CHECK(ui.Find(UiSelector::Key("status")).One().text == unchanged_status);
  replace("# 连续输入\n\nstart");
  std::string repeated = "start";
  for (int i = 0; i < 12; ++i) {
    // Simulate held-key text repeat faster than the preview interval, without
    // a pause: each 120 ms window must publish the latest text seen so far.
    ui.Find(UiSelector::Key("source-editor")).EnterText("x");
    repeated += "x";
    advance(std::chrono::milliseconds(30));
    if (i % 4 == 3) CHECK(preview().Find(UiSelector::Text(repeated)).Exists());
  }
  ui.Find(UiSelector::Key("source-editor")).EnterText("tail");
  advance(std::chrono::milliseconds(119));
  CHECK(preview().Find(UiSelector::Text(repeated)).Exists());
  advance(std::chrono::milliseconds(2));
  CHECK(preview().Find(UiSelector::Text(repeated + "tail")).Exists());
  replace("# 关闭时取消待处理输入\n");
  ui.Find(UiSelector::Key("live-toggle")).Tap();
  replace("# 手动预览\n");
  advance(std::chrono::milliseconds(200));
  CHECK(preview().Find(UiSelector::Text(repeated + "tail")).Exists());
  ui.Find(UiSelector::Key("render")).Tap();
  CHECK(preview().Find(UiSelector::Text("手动预览")).Exists());
  replace("# 重新开启\n");
  ui.Find(UiSelector::Key("live-toggle")).Tap();
  advance(std::chrono::milliseconds(121));
  CHECK(preview().Find(UiSelector::Text("重新开启")).Exists());
  replace("# 切换主题时继续更新\n");
  ui.Find(UiSelector::Key("theme-toggle")).Tap();
  ui.Find(UiSelector::Key("source-toggle")).Tap();
  advance(std::chrono::milliseconds(121));
  CHECK(preview().Find(UiSelector::Text("切换主题时继续更新")).Exists());
  ui.Find(UiSelector::Key("source-toggle")).Tap();
  replace("# 重置前的待处理输入\n");
  ui.Find(UiSelector::Key("reset")).Tap();
  advance(std::chrono::milliseconds(200));
  CHECK(preview().Find(UiSelector::Text("你好，Markdown")).Exists());
  CHECK(!preview().Find(UiSelector::Text("重置前的待处理输入")).Exists());
  ui.Find(UiSelector::Key("sample-4")).Tap();
  CHECK(preview().ScrollUntil(UiSelector::Text("第 20 节"), {.step = {0, 400}, .maximum_steps = 40}).One().in_viewport);
  replace(markdown_preview::Samples()[4].source + "\n末尾增量编辑。\n");
  advance(std::chrono::milliseconds(121));
  CHECK(preview().Find(UiSelector::Text("第 20 节")).One().in_viewport);
  CHECK(ui.Find(UiSelector::Key("parse-stats")).One().text.value_or("").starts_with("增量解析"));
  replace("# 切换前的待处理输入\n");
  ui.Find(UiSelector::Key("sample-0")).Tap();
  advance(std::chrono::milliseconds(200));
  CHECK(preview().Find(UiSelector::Text("你好，Markdown")).One().in_viewport);
  ui.SetWindowMetrics({.viewport = {480, 820}});
  ui.Pump();
  const auto source_bounds = ui.Find(UiSelector::Key("source-editor")).One().bounds;
  const auto preview_bounds = preview().One().bounds;
  CHECK(preview_bounds.y > source_bounds.y);
  CHECK(preview_bounds.x < 480 && preview_bounds.width <= 480);
}); }
