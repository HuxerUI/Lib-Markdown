#include "app.h"
#include <huxerui/markdown.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <utility>

#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
#include "document_editor_internal.h"
#include "editor.h"
#endif

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

struct WriterTab {
  std::string title;
  TextEditingValue draft;
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
  std::shared_ptr<rich_text::detail::EditSession> editor;
#endif
  std::shared_ptr<FileReference> file;
  bool operator==(const WriterTab&) const = default;
};

#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
std::optional<std::string> PrepareDocumentEditor(WriterTab& tab) {
  try {
    auto document = huxerui::markdown::detail::EditableMarkdownDocument(tab.draft.text, WriterOptions());
    if (!tab.editor) tab.editor = std::make_shared<rich_text::detail::EditSession>(std::move(document));
    else if (huxerui::markdown::detail::EditableMarkdownSource(tab.editor->Current().document) != tab.draft.text)
      tab.editor->Import(std::move(document));
    return std::nullopt;
  } catch (const std::exception& error) {
    tab.editor.reset();
    return error.what();
  }
}
#endif

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
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
  static const auto initial_edit_document = huxerui::markdown::detail::EditableMarkdownDocument(
      initial_source, WriterOptions());
#endif
  auto editing = UseState(false);
  auto preview = UseState(true);
  auto dark = UseState(false);
  auto draft = UseState(TextEditingValue::FromText(initial_source));
  auto tabs = UseState(std::vector<WriterTab>{{"开始写作", TextEditingValue::FromText(initial_source)}});
  auto selected_tab = UseState(std::size_t{0});
  auto next_tab_number = UseState(std::size_t{2});
  auto opening_file = UseState(false);
  auto saving_file = UseState(false);
  auto document = UseState(initial_document);
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
  auto status = UseState(std::string("点击正文开始编辑；按 Enter 新建段落。"));
#else
  auto status = UseState(std::string("点击正文、列表项或单元格开始编辑，Ctrl+Enter 完成。"));
#endif
  auto session = UseState(std::make_shared<WriterSession>());
  auto tasks = UseTaskScope();
  auto picker = UseService<FilePicker>();
  auto scroll = UseScrollController();
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
  auto document_editor = UseState(std::make_shared<rich_text::detail::EditSession>(initial_edit_document));
  auto document_active = UseState(true);
  auto preferred_document_mode = UseState(true);
  auto document_composing = UseState(false);
  const auto format_actions = UseState(std::make_shared<rich_text::detail::InlineFormatActions>()).Get();
#endif
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
    const auto switch_tab = [=](std::size_t index) mutable {
      if (index == selected_tab.Get() || index >= tabs.Get().size()) return;
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
      if (document_composing.Get()) { status = "请先完成输入法组合输入。"; return; }
#endif
      if (draft.Get().composition) { status = "请先完成输入法组合输入。"; return; }
      session.Get()->Cancel();
      auto updated = tabs.Get();
      updated[selected_tab.Get()].draft = draft.Get();
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
      updated[selected_tab.Get()].editor = document_editor.Get();
      const auto restore_error = PrepareDocumentEditor(updated[index]);
      document_editor = updated[index].editor;
      document_active = !editing.Get() && document_editor.Get() != nullptr;
      preferred_document_mode = true;
      if (restore_error) editing = true;
#endif
      draft = updated[index].draft;
      tabs = std::move(updated);
      selected_tab = index;
      (void)Publish(session.Get(), draft.Get().text, document, status);
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
      if (restore_error) status = "当前文件需要源码模式：" + *restore_error;
#endif
    };
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
    const bool document_mode = document_active.Get();
#else
    constexpr bool document_mode = false;
#endif
    std::vector<View> toolbar;
    const auto command_selection = [=]() -> std::optional<TextSelection> {
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
      if (document_active.Get()) return format_actions->selection ? format_actions->selection() : std::nullopt;
#endif
      return draft.Get().selection;
    };
    const auto add_command = [&](const char* title, const char* key, MarkdownEditCommand command) {
      toolbar.push_back(Button(title).On<ViewEvents::Pointer>([=](const PointerEvent& event) {
        // Touch focus dismissal collapses the editor selection before Clicked.
        // Save only its endpoints, and use them only if the source has not changed.
        if (event.type == PointerEventType::Down) {
          const auto selection = command_selection();
          session.Get()->command_selection = selection
              ? std::optional{std::pair{session.Get()->edit_revision, *selection}} : std::nullopt;
        } else if (event.type == PointerEventType::Cancel) session.Get()->command_selection.reset();
      }).OnClick([=]() mutable {
        try {
          const auto saved = std::exchange(session.Get()->command_selection, std::nullopt);
          const auto selection = saved && saved->first == session.Get()->edit_revision
              ? std::optional{saved->second} : command_selection();
          if (!selection) return;
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
          if (document_active.Get()) {
            using rich_text::detail::InlineFormat;
            const auto format = command == MarkdownEditCommand::Bold ? InlineFormat::Bold :
                command == MarkdownEditCommand::Italic ? InlineFormat::Italic : InlineFormat::Code;
            if (format_actions->toggle) format_actions->toggle(format, *selection);
            return;
          }
#endif
          auto input = draft.Get();
          input.selection = *selection;
          draft = ApplyMarkdownCommand(input, command);
          ++session.Get()->edit_revision;
          Schedule(session.Get(), tasks, draft.Get().text, document, status);
        } catch (const std::exception& error) { status = std::string("无法格式化：") + error.what(); }
      }).Key(key));
    };
    if (editing.Get() || document_mode) {
      add_command("粗体", "format-bold", MarkdownEditCommand::Bold);
      add_command("斜体", "format-italic", MarkdownEditCommand::Italic);
      add_command("代码", "format-code", MarkdownEditCommand::InlineCode);
    }
    if (editing.Get()) {
      add_command("标题", "format-heading", MarkdownEditCommand::Heading1);
      add_command("小标题", "format-heading2", MarkdownEditCommand::Heading2);
      add_command("列表", "format-list", MarkdownEditCommand::BulletList);
      add_command("引用", "format-quote", MarkdownEditCommand::BlockQuote);
    }
    const auto update_source = [=](const TextEditingValue& next) mutable {
      const bool changed = next.text != draft.Get().text;
      const bool line_count_changed = changed &&
          std::count(next.text.begin(), next.text.end(), '\n') !=
          std::count(draft.Get().text.begin(), draft.Get().text.end(), '\n');
      draft = next;
      if (changed) {
        ++session.Get()->edit_revision;
        if (line_count_changed) {
          session.Get()->Cancel();
          (void)Publish(session.Get(), next.text, document, status);
        } else Schedule(session.Get(), tasks, next.text, document, status);
      }
    };
    View body;
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
    using huxerui::markdown::detail::EditableMarkdownDocument;
    using huxerui::markdown::detail::EditableMarkdownLayout;
    using huxerui::markdown::detail::EditableMarkdownSource;
    using huxerui::markdown::detail::EditableMarkdownSyntax;
    if (document_mode) {
      body = rich_text::detail::RichTextEditor(document_editor.Get(), {
          .base_style = style.body,
          .paragraph_spacing = style.block_spacing,
          .layout = [style](const rich_text::detail::Document& doc) {
            return EditableMarkdownLayout(doc, style);
          },
          .changed = [=](const rich_text::detail::Document& doc, std::optional<TextRange> composition) mutable {
            document_composing = composition.has_value();
            const auto source = EditableMarkdownSource(doc);
            if (source == draft.Get().text) return;
            draft = TextEditingValue::FromText(std::string(source));
            document = EditableMarkdownSyntax(doc);
            ++session.Get()->edit_revision;
            status = "跨段编辑 · 草稿已更新";
          },
          .rejected = [status](std::string_view error) mutable {
            status = "无法跨段编辑：" + std::string(error);
          },
          .format_actions = format_actions,
      }).Key("writer-document");
    } else
#endif
    if (editing.Get()) {
      View source = TextField(draft).LineLimits(TextFieldLineLimits::MultiLine(1))
          .VerticalAlign(TextVerticalAlign::Top).OnChanged(update_source).Key("writer-source");
      if (preview.Get()) {
        View rendered = VirtualizedMarkdown(document.Get(), {.style = style}, {.scroll_controller = scroll})
            .On<MarkdownEvents::LinkActivated>([status](const MarkdownLinkActivation& event) mutable {
              status = "链接：" + event.destination;
            }).Key("writer-preview");
        body = compact ? View(Column{std::move(source).With(Grow{}), std::move(rendered).With(Grow{})}.With(Spacing(16.0F), CrossAlign(CrossAxisAlignment::Stretch)))
                       : View(Row{std::move(source).With(Grow{}), std::move(rendered).With(Grow{})}.With(Spacing(24.0F), CrossAlign(CrossAxisAlignment::Stretch)));
      } else body = std::move(source);
    } else {
      body = MarkdownEditor(draft, {.parse_options = WriterOptions(), .markdown_options = {.style = style},
          .virtualization = {.scroll_controller = scroll}, .live_preview = preview.Get()})
          .On<MarkdownEditorEvents::Changed>(update_source)
          .On<MarkdownEditorEvents::Error>([status](const std::string& error) mutable {
            status = "无法编辑：" + error;
          }).Key("writer-editor");
    }
    std::vector<StringVariant> tab_labels;
    for (const auto& tab : tabs.Get()) tab_labels.emplace_back(tab.title);
    toolbar.push_back(Button(editing.Get() ? "原位编辑" : "编辑源码").OnClick([=]() mutable {
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
          if (document_composing.Get()) { status = "请先完成输入法组合输入。"; return; }
#endif
          if (draft.Get().composition) { status = "请先完成输入法组合输入。"; return; }
          session.Get()->Cancel();
          if (Publish(session.Get(), draft.Get().text, document, status)) {
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
            if (editing.Get() && preferred_document_mode.Get()) {
              try {
                auto editor = document_editor.Get();
                if (!editor) editor = std::make_shared<rich_text::detail::EditSession>(
                    EditableMarkdownDocument(draft.Get().text, WriterOptions()));
                else if (EditableMarkdownSource(editor->Current().document) != draft.Get().text)
                  editor->Import(EditableMarkdownDocument(draft.Get().text, WriterOptions()));
                document = EditableMarkdownSyntax(editor->Current().document);
                document_editor = std::move(editor);
                document_active = true;
              } catch (const std::exception& error) {
                status = std::string("无法进入原位编辑：") + error.what();
                return;
              }
            } else document_active = false;
#endif
            editing = !editing.Get();
          }
        }).Key("mode-toggle"));
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
    toolbar.push_back(Button(document_mode ? "单块编辑" : "整篇编辑").OnClick([=]() mutable {
          if (draft.Get().composition || document_composing.Get()) { status = "请先完成输入法组合输入。"; return; }
          if (document_active.Get()) {
            document_active = false;
            preferred_document_mode = false;
            status = "已切换单块编辑";
            return;
          }
          try {
            auto editor = document_editor.Get();
            const auto& source = draft.Get().text;
            if (!editor) {
              editor = std::make_shared<rich_text::detail::EditSession>(EditableMarkdownDocument(source, WriterOptions()));
            } else if (EditableMarkdownSource(editor->Current().document) != source) {
              editor->Import(EditableMarkdownDocument(source, WriterOptions()));
            }
            session.Get()->Cancel();
            document = EditableMarkdownSyntax(editor->Current().document);
            document_editor = std::move(editor);
            document_active = true;
            preferred_document_mode = true;
            editing = false;
            status = "跨段编辑 · 选中标题和正文后输入，Ctrl+Z 撤销";
          } catch (const std::invalid_argument&) {
            status = "当前草稿没有可定位的文字块。可用块内编辑或源码模式。";
          } catch (const std::length_error& error) {
            status = std::string("无法跨段编辑：") + error.what();
          }
        }).Key("document-mode-toggle"));
#endif
    toolbar.push_back(Checkbox("实时预览", preview.Get()).OnChanged([preview](bool enabled) mutable { preview = enabled; }).Key("writer-preview-toggle"));
    toolbar.push_back(Button("打开文件").With(Enabled(!opening_file.Get() && picker->CanOpenFiles()))
        .OnClick([=]() mutable {
          opening_file = true;
          tasks.Launch([=]() mutable -> Task<void> {
            const FilePickerFilter filter{.name = "Markdown files", .extensions = {"md", "markdown", "mdown", "txt"},
                .content_types = {"text/markdown", "text/plain"}};
            auto selected = co_await picker->OpenFileAsync(filter);
            if (!selected) { opening_file = false; co_return; }
            auto result = co_await selected->ReadStringAsync();
            if (!result.Succeeded()) {
              status = "无法打开文件：" + result.Error().message;
              opening_file = false;
              co_return;
            }
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
            if (document_composing.Get()) {
              status = "请先完成输入法组合输入。";
              opening_file = false;
              co_return;
            }
#endif
            if (draft.Get().composition) {
              status = "请先完成输入法组合输入。";
              opening_file = false;
              co_return;
            }
            session.Get()->Cancel();
            auto updated = tabs.Get();
            updated[selected_tab.Get()].draft = draft.Get();
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
            updated[selected_tab.Get()].editor = document_editor.Get();
#endif
            const auto title = selected->Name().empty() ? "未命名文档" : selected->Name();
            WriterTab opened{title, TextEditingValue::FromText(std::move(result).Value())};
            opened.file = std::make_shared<FileReference>(std::move(*selected));
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
            try {
              opened.editor = std::make_shared<rich_text::detail::EditSession>(
                  EditableMarkdownDocument(opened.draft.text, WriterOptions()));
            } catch (const std::exception&) { editing = true; }
            document_editor = opened.editor;
            document_active = opened.editor != nullptr;
            preferred_document_mode = document_active.Get();
            if (document_active.Get()) editing = false;
#endif
            updated.push_back(std::move(opened));
            selected_tab = updated.size() - 1;
            draft = updated.back().draft;
            tabs = std::move(updated);
            (void)Publish(session.Get(), draft.Get().text, document, status);
            opening_file = false;
          });
        }).Key("writer-open-file"));
    const auto current_file = tabs.Get().at(selected_tab.Get()).file;
    toolbar.push_back(Button("保存").With(Enabled(current_file && current_file->CanWrite() && !saving_file.Get()))
        .OnClick([=]() mutable {
          saving_file = true;
          const auto contents = draft.Get().text;
          tasks.Launch([=]() mutable -> Task<void> {
            auto opened = co_await current_file->OpenWriteAsync();
            if (!opened.Succeeded()) {
              status = "无法保存文件：" + opened.Error().message;
              saving_file = false;
              co_return;
            }
            auto writer = std::move(opened).Value();
            Bytes data(contents.size());
            if (!contents.empty()) std::memcpy(data.data(), contents.data(), contents.size());
            auto written = co_await writer.WriteAsync(std::move(data));
            if (!written.Succeeded()) {
              status = "无法保存文件：" + written.Error().message;
              saving_file = false;
              co_return;
            }
            auto closed = co_await writer.CloseAsync();
            status = closed.Succeeded() ? "已保存 " + current_file->Name() :
                "无法保存文件：" + closed.Error().message;
            saving_file = false;
          });
        }).Key("writer-save-file"));
    toolbar.push_back(Button(dark.Get() ? "浅色" : "深色").OnClick([dark]() mutable { dark = !dark.Get(); }).Key("writer-theme"));
    return Column{
      WindowTitleBar{
        Text("Markdown Writer").Style({Font::System(16).WithWeight(FontWeight::Bold), theme.colors.on_surface}),
        Tabs(std::move(tab_labels), selected_tab.Get()).OnChanged(switch_tab).With(Grow{}).Key("writer-tabs"),
        Button("+").OnClick([=]() mutable {
          if (draft.Get().composition) { status = "请先完成输入法组合输入。"; return; }
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
          if (document_composing.Get()) { status = "请先完成输入法组合输入。"; return; }
#endif
          session.Get()->Cancel();
          auto updated = tabs.Get();
          updated[selected_tab.Get()].draft = draft.Get();
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
          updated[selected_tab.Get()].editor = document_editor.Get();
#endif
          const auto title = "文档 " + std::to_string(next_tab_number.Get());
          next_tab_number = next_tab_number.Get() + 1;
          WriterTab opened{title, TextEditingValue::FromText("# " + title + "\n\n")};
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
          opened.editor = std::make_shared<rich_text::detail::EditSession>(
              EditableMarkdownDocument(opened.draft.text, WriterOptions()));
          document_editor = opened.editor;
          document_active = true;
          preferred_document_mode = true;
          editing = false;
#endif
          updated.push_back(std::move(opened));
          selected_tab = updated.size() - 1;
          draft = updated.back().draft;
          tabs = std::move(updated);
          (void)Publish(session.Get(), draft.Get().text, document, status);
        }).Key("writer-new-tab"),
        Button("×").OnClick([=]() mutable {
          if (tabs.Get().size() == 1) return;
          if (draft.Get().composition) { status = "请先完成输入法组合输入。"; return; }
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
          if (document_composing.Get()) { status = "请先完成输入法组合输入。"; return; }
          document_active = false;
#endif
          session.Get()->Cancel();
          auto updated = tabs.Get();
          updated.erase(updated.begin() + selected_tab.Get());
          const auto index = std::min(selected_tab.Get(), updated.size() - 1);
          selected_tab = index;
          draft = updated[index].draft;
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
          const auto restore_error = PrepareDocumentEditor(updated[index]);
          document_editor = updated[index].editor;
          document_active = !editing.Get() && document_editor.Get() != nullptr;
          preferred_document_mode = true;
          if (restore_error) editing = true;
#endif
          tabs = std::move(updated);
          (void)Publish(session.Get(), draft.Get().text, document, status);
#ifdef HUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE
          if (restore_error) status = "当前文件需要源码模式：" + *restore_error;
#endif
        }).Key("writer-close-tab"),
      }.With(Padding(EdgeInsets::Symmetric(compact ? 8.0F : 16.0F, 4.0F)), Spacing(8.0F), Background{theme.colors.surface}),
      Flow(std::move(toolbar)).With(Padding(EdgeInsets::Symmetric(compact ? 8.0F : 16.0F, 6.0F)), Spacing(8.0F)).Key("writer-toolbar"),
      std::move(body).With(Grow{}, Padding(compact ? 8.0F : 16.0F), Background{theme.colors.surface}, Border{theme.colors.outline, 0.5F}),
      Text(status).With(Padding(EdgeInsets::Symmetric(12.0F, 4.0F)), Foreground{theme.colors.on_surface_variant}).Key("writer-status"),
    }.With(CrossAlign(CrossAxisAlignment::Stretch), Background{theme.colors.background});
  }));
}
} // namespace markdown_writer

const huxerui::Application application{markdown_writer::App,
    {.window = {.title = "Markdown Writer", .initial_size = {1180, 820},
                .chrome_mode = huxerui::WindowChromeMode::Custom, .title_bar_height = 48.0F},
     .show_debug_overlay = false}};
