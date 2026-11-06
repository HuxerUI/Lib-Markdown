#include "app.h"
#include <huxerui/markdown.h>
#include <chrono>
#include <cstdint>
#include <utility>

namespace markdown_writer {
using namespace huxerui;
using namespace huxerui::markdown;

const std::string initial_source = R"md(# 开始写作

这是类 Typora 编辑体验的开发 demo。直接点击标题、列表正文、引用或表格单元格，输入时保持样式。

## 试一试

点击这一段，在 **粗体**、*斜体* 或 `代码` 中修改文字，点击其他位置或按 Ctrl+Enter 完成编辑。

- 点击 **列表正文** 直接修改
  - [x] 嵌套任务保留缩进和勾选状态
- 连续输入时实时更新内容和选区

> [!NOTE]
> 工具栏命令来自 Lib-Markdown，编辑模式不是独立的一套解析器。

| 内容 | 状态 |
| --- | ---: |
| **点这里编辑** | 可修改 |
| 空单元格 → |  |

```cpp
const auto greeting = "Hello, Markdown";
```

阅读 [项目文档](../guide.md)，也可以输入行内公式源码 $x^2$。
)md";

ParseOptions WriterOptions() {
  return {.highlight = true, .insertions = true, .underline = true, .wiki_links = true,
          .admonitions = true, .footnotes = true, .math = true, .preserve_blank_lines = true, .spoilers = true};
}

struct WriterSession {
  MarkdownParseSession parser{WriterOptions()};
  TaskHandle pending;
  std::uint64_t request = 0;
  bool scheduled = false;
  std::string latest;
  std::uint64_t edit_revision = 0;
  std::optional<std::pair<std::uint64_t, TextSelection>> command_selection;
  void Cancel() { pending.Cancel(); ++request; scheduled = false; latest.clear(); }
};

bool Publish(const std::shared_ptr<WriterSession>& session, const std::string& source,
             State<MarkdownDocument> document, State<std::string> status) {
  try {
    document = session->parser.Update(source);
    const auto stats = session->parser.LastParseStats();
    status = "已更新 · 重解析 " + std::to_string(stats.parsed_bytes) + " 字节 · 复用 " + std::to_string(stats.reused_blocks) + " 块";
    return true;
  } catch (const std::exception& error) { status = std::string("无法预览：") + error.what(); return false; }
}

void Schedule(const std::shared_ptr<WriterSession>& session, TaskScope tasks, std::string source,
              State<MarkdownDocument> document, State<std::string> status) {
  session->latest = std::move(source);
  if (session->scheduled) return;
  session->scheduled = true;
  const auto request = session->request;
  session->pending = tasks.Launch([=]() -> Task<void> {
    co_await Delay(std::chrono::milliseconds(120));
    if (session->request != request) co_return;
    session->scheduled = false;
    auto latest = std::move(session->latest);
    (void)Publish(session, latest, document, status);
  });
}

View App() {
  static const auto initial_document = ParseMarkdown(initial_source, WriterOptions());
  auto editing = UseState(false);
  auto preview = UseState(true);
  auto dark = UseState(false);
  auto draft = UseState(TextEditingValue::FromText(initial_source));
  auto document = UseState(initial_document);
  auto status = UseState(std::string("点击正文、列表项或单元格开始编辑，Ctrl+Enter 完成。"));
  auto session = UseState(std::make_shared<WriterSession>());
  auto tasks = UseTaskScope();
  auto scroll = UseScrollController();
  Lifecycle([=] {
    (void)Publish(session.Get(), initial_source, document, status);
    return [session] { session.Get()->Cancel(); };
  });
  return MaterialTheme(dark.Get() ? MaterialDarkThemeSpec() : MaterialLightThemeSpec(), Scope([=] {
    const auto theme = UseTheme();
    const bool compact = UseViewportClass() == ViewportClass::Compact;
    auto style = MarkdownStyle::FromTheme(theme);
    style.body.font = style.body.font.WithSize(18);
    style.blank_line_height = 24;
    View rendered = VirtualizedMarkdown(document.Get(), {.style = style}, {.scroll_controller = scroll})
        .On<MarkdownEvents::LinkActivated>([status](const MarkdownLinkActivation& event) mutable {
          status = "链接：" + event.destination;
        }).Key("writer-preview");
    std::vector<View> toolbar;
    const auto add_command = [&](const char* title, const char* key, MarkdownEditCommand command) {
      toolbar.push_back(Button(title).On<ViewEvents::Pointer>([=](const PointerEvent& event) {
        // Touch focus dismissal collapses the editor selection before Clicked.
        // Save only its endpoints, and use them only if the source has not changed.
        if (event.type == PointerEventType::Down)
          session.Get()->command_selection = std::pair{session.Get()->edit_revision, draft.Get().selection};
        else if (event.type == PointerEventType::Cancel) session.Get()->command_selection.reset();
      }).OnClick([=]() mutable {
        try {
          auto input = draft.Get();
          const auto saved = std::exchange(session.Get()->command_selection, std::nullopt);
          if (saved && saved->first == session.Get()->edit_revision) input.selection = saved->second;
          draft = ApplyMarkdownCommand(input, command);
          ++session.Get()->edit_revision;
          Schedule(session.Get(), tasks, draft.Get().text, document, status);
        } catch (const std::exception& error) { status = std::string("无法格式化：") + error.what(); }
      }).Key(key));
    };
    if (editing.Get()) {
      add_command("粗体", "format-bold", MarkdownEditCommand::Bold);
      add_command("斜体", "format-italic", MarkdownEditCommand::Italic);
      add_command("代码", "format-code", MarkdownEditCommand::InlineCode);
      add_command("标题", "format-heading", MarkdownEditCommand::Heading1);
      add_command("小标题", "format-heading2", MarkdownEditCommand::Heading2);
      add_command("列表", "format-list", MarkdownEditCommand::BulletList);
      add_command("引用", "format-quote", MarkdownEditCommand::BlockQuote);
    }
    View body = MarkdownEditor(draft, {.parse_options = WriterOptions(), .markdown_options = {.style = style},
        .virtualization = {.scroll_controller = scroll}, .live_preview = preview.Get()})
        .On<MarkdownEditorEvents::Changed>([=](const TextEditingValue& next) mutable {
          const bool changed = next.text != draft.Get().text;
          draft = next;
          if (changed) {
            ++session.Get()->edit_revision;
            Schedule(session.Get(), tasks, next.text, document, status);
          }
        }).On<MarkdownEditorEvents::Error>([status](const std::string& error) mutable {
          status = "无法编辑：" + error;
        }).Key("writer-editor");
    if (editing.Get()) {
      body = std::move(rendered);
      View source = TextField(draft).LineLimits(TextFieldLineLimits::MultiLine(1))
          .VerticalAlign(TextVerticalAlign::Top).OnChanged([=](const TextEditingValue& next) mutable {
            const bool changed = next.text != draft.Get().text;
            draft = next;
            if (changed) {
              ++session.Get()->edit_revision;
              Schedule(session.Get(), tasks, next.text, document, status);
            }
          }).Key("writer-source");
      if (preview.Get()) {
        body = compact ? View(Column{std::move(source).With(Grow{}), std::move(body).With(Grow{})}.With(Spacing(16.0F), CrossAlign(CrossAxisAlignment::Stretch)))
                       : View(Row{std::move(source).With(Grow{}), std::move(body).With(Grow{})}.With(Spacing(24.0F), CrossAlign(CrossAxisAlignment::Stretch)));
      } else body = std::move(source);
    }
    return Column{
      Row{
        Text("Markdown Writer · 实验版").Style({Font::System(24).WithWeight(FontWeight::Bold), theme.colors.on_surface}).With(Grow{}),
        Button(editing.Get() ? "原位编辑" : "编辑源码").OnClick([=]() mutable {
          if (draft.Get().composition) { status = "请先完成输入法组合输入。"; return; }
          session.Get()->Cancel();
          if (Publish(session.Get(), draft.Get().text, document, status)) editing = !editing.Get();
        }).Key("mode-toggle"),
        Button(dark.Get() ? "浅色" : "深色").OnClick([dark]() mutable { dark = !dark.Get(); }).Key("writer-theme"),
      }.With(Spacing(12.0F)),
      Text("类 Typora 开发 demo · 保持排版直接编辑 · 点击离开或 Ctrl+Enter 完成")
          .With(Foreground{theme.colors.on_surface_variant}),
      Flow(std::move(toolbar)).With(Spacing(8.0F)),
      Checkbox("源码实时预览", preview.Get()).OnChanged([preview](bool enabled) mutable { preview = enabled; }).Key("writer-preview-toggle"),
      std::move(body).With(Grow{}, Padding(16.0F), Background{theme.colors.surface}, Border{theme.colors.outline, 0.5F}),
      Text(status).With(Foreground{theme.colors.on_surface_variant}).Key("writer-status"),
    }.With(Padding(compact ? 12.0F : 24.0F), Spacing(12.0F), CrossAlign(CrossAxisAlignment::Stretch), Background{theme.colors.background});
  }));
}
} // namespace markdown_writer

const huxerui::Application application{markdown_writer::App,
    {.window = {.title = "Markdown Writer", .initial_size = {1180, 820}}, .show_debug_overlay = false}};
