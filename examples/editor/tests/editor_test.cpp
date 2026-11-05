#include "../src/app.h"
#include "../../../tests/test.h"
#include <huxerui/testing/ui_test.h>
#include <chrono>

using namespace huxerui;
using namespace huxerui::testing;

class WriterResources final : public PlatformResources {
public:
  explicit WriterResources(std::string root) : root_(std::move(root)) {}
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
  UiTest ui(application, {.viewport = {1180, 820}, .resource_provider = std::make_shared<WriterResources>(argv[1])});
  const auto advance = [&](int ms) { ui.Pump(std::chrono::milliseconds(ms)); ui.Pump(); };
  CHECK(ui.Find(UiSelector::Text("开始写作")).Exists());
  CHECK(!ui.Find(UiSelector::Key("writer-source")).Exists());
  ui.Find(UiSelector::Key("mode-toggle")).Tap();
  auto source = ui.Find(UiSelector::Key("writer-source"));
  source.Tap();
  source.ReplaceText("中文😀");
  source.SetSelection({0, 4});
  ui.Find(UiSelector::Key("format-bold")).Tap();
  if (source.One().value != "**中文😀**")
    throw std::runtime_error("Formatting result: " + source.One().value.value_or("") + " / " + ui.Find(UiSelector::Key("writer-status")).One().text.value_or(""));
  CHECK(source.One().value == "**中文😀**");
  advance(121);
  CHECK(ui.Find(UiSelector::Key("writer-preview")).Find(UiSelector::Text("中文😀")).Exists());
  ui.Find(UiSelector::Key("mode-toggle")).Tap();
  CHECK(!source.Exists());
  CHECK(ui.Find(UiSelector::Text("中文😀")).Exists());
  ui.Find(UiSelector::Key("writer-theme")).Tap();
  CHECK(ui.Find(UiSelector::Text("中文😀")).Exists());
  ui.Find(UiSelector::Key("mode-toggle")).Tap();
  CHECK(source.One().value == "**中文😀**");
  source.Tap();
  source.ReplaceText("# 持续输入\n\na");
  std::string repeated = "a";
  for (int i = 0; i < 8; ++i) {
    source.EnterText("x");
    repeated += "x";
    advance(30);
    if (i % 4 == 3) CHECK(ui.Find(UiSelector::Key("writer-preview")).Find(UiSelector::Text(repeated)).Exists());
  }
  source.ReplaceText("# 切换时立即更新\n");
  ui.Find(UiSelector::Key("mode-toggle")).Tap(); // Flush queued input immediately.
  CHECK(ui.Find(UiSelector::Text("切换时立即更新")).Exists());
  advance(200);
  CHECK(!ui.Find(UiSelector::Text("持续输入")).Exists());
  ui.Find(UiSelector::Key("mode-toggle")).Tap();
  ui.Find(UiSelector::Key("writer-preview-toggle")).Tap();
  CHECK(!ui.Find(UiSelector::Key("writer-preview")).Exists());
  CHECK(source.Exists());
  ui.Find(UiSelector::Key("writer-preview-toggle")).Tap();
  ui.SetWindowMetrics({.viewport = {480, 820}});
  ui.Pump();
  CHECK(ui.Find(UiSelector::Key("writer-preview")).One().bounds.y > source.One().bounds.y);
}); }
