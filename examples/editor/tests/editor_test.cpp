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
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
  CHECK(ui.Find(UiSelector::Key("rich-text-editor")).Find(UiSelector::Text("开始写作")).Exists());
#else
  CHECK(ui.Find(UiSelector::Value("开始写作")).Exists());
#endif
  CHECK(!ui.Find(UiSelector::Key("writer-source")).Exists());
  CHECK(ui.Find(UiSelector::Key("writer-tabs")).Exists());
  CHECK(ui.Find(UiSelector::Key("writer-logo")).Exists());
  CHECK(!ui.Find(UiSelector::Text("Markdown Writer")).Exists());
  CHECK(!ui.Find(UiSelector::Key("writer-close-tab-1")).Exists());
  CHECK(ui.Find(UiSelector::Key("writer-toolbar")).Exists());
  CHECK(ui.Find(UiSelector::Key("writer-open-file")).Exists());
  CHECK(ui.Find(UiSelector::Key("writer-save-file")).Exists());
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
  auto initial_surface = ui.Find(UiSelector::Key("rich-text-editor"));
  initial_surface.Find(UiSelector::Text("开始写作")).Tap();
  const auto enter_client = ui.ActiveTextInput()->session_id;
  CHECK(ui.SendTextInput({enter_client, {{.kind = TextInputCommandKind::SetSelection,
      .selection_after = TextSelection{4, 4}}}}).result_code == TextInputResultCode::Ok);
  ui.PressKey(Key::Enter);
  CHECK(ui.ActiveTextInput()->session_id == enter_client);
  CHECK(ui.SendTextInput({enter_client, {{.kind = TextInputCommandKind::CommitText,
      .text = "回车新段落"}}}).result_code == TextInputResultCode::Ok);
  ui.Pump();
  CHECK(initial_surface.Find(UiSelector::Text("回车新段落")).Exists());
  ui.PressKey(Key::Z, {.control = true});
  ui.PressKey(Key::Z, {.control = true});
  ui.Find(UiSelector::Key("mode-toggle")).Tap();
  CHECK(ui.Find(UiSelector::Key("writer-source")).Exists());
  ui.Find(UiSelector::Key("mode-toggle")).Tap();
  CHECK(initial_surface.Exists());
  ui.Find(UiSelector::Key("document-mode-toggle")).Tap();
  CHECK(!initial_surface.Exists());
#endif
  ui.Find(UiSelector::Key("writer-new-tab")).Tap();
  CHECK(!ui.Find(UiSelector::Key("writer-close-tab-2")).Exists());
  const auto second_tab_bounds = ui.Find(UiSelector::Key("writer-tab-2")).One().bounds;
  ui.SendPointer({.type = PointerEventType::Move, .pointer_id = 40,
      .position = {second_tab_bounds.x + 12.0F, second_tab_bounds.y + second_tab_bounds.height / 2.0F},
      .device_kind = PointerDeviceKind::Mouse});
  ui.Pump();
  CHECK(ui.Find(UiSelector::Key("writer-close-tab-2")).Exists());
  ui.SendPointer({.type = PointerEventType::Move, .pointer_id = 40, .position = {600.0F, 100.0F},
      .device_kind = PointerDeviceKind::Mouse});
  ui.Pump();
  CHECK(!ui.Find(UiSelector::Key("writer-close-tab-2")).Exists());
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
  CHECK(ui.Find(UiSelector::Key("rich-text-editor")).Exists());
#endif
  ui.Find(UiSelector::Key("mode-toggle")).Tap();
  auto tab_source = ui.Find(UiSelector::Key("writer-source"));
  CHECK(tab_source.One().value == "# 文档 2\n\n");
  tab_source.Tap();
  tab_source.ReplaceText("# 第二篇\n\n独立草稿");
  ui.Find(UiSelector::Key("writer-tabs")).Find(UiSelector::Text("开始写作")).Tap();
  CHECK(tab_source.One().value->starts_with("# 开始写作\n"));
  ui.Find(UiSelector::Key("writer-tabs")).Find(UiSelector::Text("文档 2")).Tap();
  CHECK(tab_source.One().value == "# 第二篇\n\n独立草稿");
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
  ui.Find(UiSelector::Key("mode-toggle")).Tap();
  const auto switched_surface = ui.Find(UiSelector::Key("rich-text-editor"));
  CHECK(switched_surface.Find(UiSelector::Text("独立草稿")).Exists());
  ui.Find(UiSelector::Key("writer-tabs")).Find(UiSelector::Text("开始写作")).Tap();
  CHECK(switched_surface.Find(UiSelector::Text("开始写作")).Exists());
  CHECK(!switched_surface.Find(UiSelector::Text("独立草稿")).Exists());
  ui.Find(UiSelector::Key("writer-tabs")).Find(UiSelector::Text("文档 2")).Tap();
  CHECK(switched_surface.Find(UiSelector::Text("独立草稿")).Exists());
  CHECK(!switched_surface.Find(UiSelector::Text("开始写作")).Exists());
  ui.Find(UiSelector::Key("mode-toggle")).Tap();
#endif
  const auto close_tab_bounds = ui.Find(UiSelector::Key("writer-tab-2")).One().bounds;
  ui.SendPointer({.type = PointerEventType::Move, .pointer_id = 40,
      .position = {close_tab_bounds.x + 12.0F, close_tab_bounds.y + close_tab_bounds.height / 2.0F},
      .device_kind = PointerDeviceKind::Mouse});
  ui.Pump();
  ui.Find(UiSelector::Key("writer-close-tab-2")).Tap();
  CHECK(tab_source.One().value->starts_with("# 开始写作\n"));
  ui.Find(UiSelector::Key("mode-toggle")).Tap();
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
  ui.Find(UiSelector::Key("document-mode-toggle")).Tap();
#endif
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
  source.ReplaceText("# 回车立即更新");
  advance(121);
  source.SetSelection({AttributedText(*source.One().value).Length(), AttributedText(*source.One().value).Length()});
  ui.PressKey(Key::Enter);
  CHECK(source.One().value == "# 回车立即更新\n");
  CHECK(ui.Find(UiSelector::Key("writer-status")).One().text->starts_with("已更新"));
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
  source.ReplaceText("```\r\nprotected\r\n```\r\n");
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

  source.Tap();
  source.ReplaceText("# Mix\r\n\r\n- **parent**\r\n  - [x] nested\r\n\r\n> quote\r\n\r\n"
      "| A | B |\r\n| --- | ---: |\r\n| c | right |\r\n");
  experiment.Tap();
  CHECK(surface.Exists());
  ui.Find(UiSelector::Text("nested")).Tap({.device_kind = PointerDeviceKind::Mouse});
  CHECK(ui.ActiveTextInput().has_value());
  const auto mixed_client = ui.ActiveTextInput()->session_id;
  CHECK(ui.ActiveTextInput()->selection.active >= 11 && ui.ActiveTextInput()->selection.active <= 17);
  CHECK(ui.SendTextInput({mixed_client, {{.kind = TextInputCommandKind::CommitText, .target = TextRange{11, 17}, .text = "中文"}}})
      .result_code == TextInputResultCode::Ok);
  ui.Pump();
  CHECK(ui.Find(UiSelector::Text("中文")).Exists());
  const auto cell = ui.Find(UiSelector::Text("right")).One().bounds;
  ui.TapAt({cell.x + cell.width - 2, cell.y + cell.height / 2}, {.device_kind = PointerDeviceKind::Mouse});
  CHECK(ui.ActiveTextInput()->session_id == mixed_client);
  CHECK(ui.ActiveTextInput()->selection.IsCollapsed() && ui.ActiveTextInput()->selection.active == 31);
  CHECK(ui.SendTextInput({mixed_client, {{.kind = TextInputCommandKind::CommitText, .text = "末"}}})
      .result_code == TextInputResultCode::Ok);
  ui.Pump();
  CHECK(ui.Find(UiSelector::Text("right末")).Exists());
  ui.Find(UiSelector::Key("mode-toggle")).Tap();
  CHECK(source.One().value->find("  - [x] 中文\r\n") != std::string::npos);
  CHECK(source.One().value->find("| c | right末 |\r\n") != std::string::npos);
  const auto mixed_draft = source.One().value.value();
  experiment.Tap();
  ui.Find(UiSelector::Text("right末")).Tap({.device_kind = PointerDeviceKind::Mouse});
  ui.PressKey(Key::Z, {.control = true});
  CHECK(ui.Find(UiSelector::Text("right")).Exists());
  CHECK(ui.ActiveTextInput()->selection.active == 31);
  ui.PressKey(Key::Y, {.control = true});
  CHECK(ui.Find(UiSelector::Text("right末")).Exists());
  ui.Find(UiSelector::Key("mode-toggle")).Tap();
  CHECK(source.One().value == mixed_draft);

  const auto imported_draft = mixed_draft + "\r\nImported paragraph\r\n";
  source.Tap();
  source.ReplaceText(imported_draft);
  experiment.Tap();
  CHECK(ui.Find(UiSelector::Text("Imported paragraph")).Exists());
  ui.Find(UiSelector::Text("Mix")).Tap({.device_kind = PointerDeviceKind::Mouse});
  ui.PressKey(Key::Z, {.control = true});
  CHECK(!ui.Find(UiSelector::Text("Imported paragraph")).Exists());
  CHECK(ui.Find(UiSelector::Text("right末")).Exists());
  ui.PressKey(Key::Y, {.control = true});
  CHECK(ui.Find(UiSelector::Text("Imported paragraph")).Exists());
  ui.Find(UiSelector::Key("mode-toggle")).Tap();
  CHECK(source.One().value == imported_draft);
#endif
}); }
