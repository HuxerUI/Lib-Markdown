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
  CHECK(ui.Find(UiSelector::Value("开始写作")).Exists());
  CHECK(!ui.Find(UiSelector::Key("writer-source")).Exists());
  auto block = ui.Find(UiSelector::AllOf(UiSelector::Key("markdown-block-input"), UiSelector::Focused(true)));
  ui.Find(UiSelector::Value("嵌套任务保留缩进和勾选状态")).Tap();
  CHECK(block.One().value == "嵌套任务保留缩进和勾选状态");
  block.ReplaceText("任务已原位修改");
  CHECK(!ui.Find(UiSelector::Key("markdown-block-preview")).Exists());
  ui.Find(UiSelector::Value("开始写作")).Tap();
  CHECK(block.One().value == "开始写作");
  block.ReplaceText("原位输入😀");
  CHECK(block.One().value == "原位输入😀");
  CHECK(!ui.Find(UiSelector::Key("markdown-block-preview")).Exists());
  ui.Find(UiSelector::Key("mode-toggle")).Tap();
  auto source = ui.Find(UiSelector::Key("writer-source"));
  CHECK(source.One().value->starts_with("# 原位输入😀\n"));
  CHECK(source.One().value->find("  - [x] 任务已原位修改\n") != std::string::npos);
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
  CHECK(ui.Find(UiSelector::Value("中文😀")).Exists());
  ui.Find(UiSelector::Key("writer-theme")).Tap();
  CHECK(ui.Find(UiSelector::Value("中文😀")).Exists());
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
  CHECK(ui.Find(UiSelector::Value("切换时立即更新")).Exists());
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

#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
  ui.SetWindowMetrics({.viewport = {1180, 820}});
  ui.Pump();
  const auto experiment = ui.Find(UiSelector::Key("document-mode-toggle"));
  const auto surface = ui.Find(UiSelector::Key("rich-text-editor"));
  source.Tap();
  source.ReplaceText("# Keep\r\n\r\n- item\r\n");
  experiment.Tap();
  CHECK(!surface.Exists() && source.Exists());

  const std::string prose = "# Title\r\n\r\nBody\r\n\r\n*After*\r\n";
  source.Tap();
  source.ReplaceText(prose);
  experiment.Tap();
  CHECK(surface.Exists() && !source.Exists());
  const auto first = ui.Find(UiSelector::Text("Title")).One().bounds;
  const auto second = ui.Find(UiSelector::Text("Body")).One().bounds;
  ui.Drag({first.x + 20, first.y + first.height / 2},
          {second.x + 20, second.y + second.height / 2},
          {.pointer = {.device_kind = PointerDeviceKind::Mouse}});
  CHECK(ui.ActiveTextInput().has_value());
  const auto client = ui.ActiveTextInput()->session_id;
  CHECK(ui.ActiveTextInput()->selection.Range().start < 5);
  CHECK(ui.ActiveTextInput()->selection.Range().end > 6);
  const TextSelection across{2, 8};
  CHECK(ui.SendTextInput({client, {{.kind = TextInputCommandKind::SetSelection, .selection_after = across}}})
      .result_code == TextInputResultCode::Ok);
  CHECK(ui.SendTextInput({client, {{.kind = TextInputCommandKind::UpdateComposition, .text = "拼"}}})
      .result_code == TextInputResultCode::Ok);
  CHECK(ui.ActiveTextInput()->composition == (TextRange{2, 3}));
  CHECK(ui.SendTextInput({client, {{.kind = TextInputCommandKind::CommitText, .text = "中文"}}})
      .result_code == TextInputResultCode::Ok);
  ui.Pump();
  CHECK(ui.Find(UiSelector::Text("Ti中文dy")).Exists());
  ui.PressKey(Key::Z, {.control = true});
  CHECK(ui.ActiveTextInput()->selection == across);
  CHECK(ui.Find(UiSelector::Text("Title")).Exists() && ui.Find(UiSelector::Text("Body")).Exists());
  ui.PressKey(Key::Y, {.control = true});
  CHECK(ui.SendTextInput({client, {{.kind = TextInputCommandKind::SetSelection, .selection_after = TextSelection{6, 6}}}})
      .result_code == TextInputResultCode::Ok);
  ui.PressKey(Key::Enter);
  CHECK(ui.ActiveTextInput()->selection == (TextSelection{7, 7}));
  ui.PressKey(Key::Backspace);
  CHECK(ui.ActiveTextInput()->selection == (TextSelection{6, 6}));
  const TextSelection heading_selection{6, 0, TextAffinity::Upstream};
  CHECK(ui.SendTextInput({client, {{.kind = TextInputCommandKind::SetSelection, .selection_after = heading_selection}}})
      .result_code == TextInputResultCode::Ok);
  ui.Find(UiSelector::Key("format-bold")).Tap();
  ui.Find(UiSelector::Text("Ti中文dy")).Tap();
  ui.PressKey(Key::Z, {.control = true});
  CHECK(ui.ActiveTextInput()->selection == heading_selection);
  ui.PressKey(Key::Y, {.control = true});
  ui.Find(UiSelector::Key("writer-theme")).Tap();
  CHECK(surface.Exists() && ui.Find(UiSelector::Text("Ti中文dy")).Exists());
  ui.Find(UiSelector::Key("mode-toggle")).Tap();
  CHECK(!surface.Exists() && source.One().value == "# **Ti中文dy**\r\n\r\n*After*\r\n");
#endif
}); }
