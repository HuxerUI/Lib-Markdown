# 迁移到 0.3

0.3 是一次源码不兼容裁剪：库只保留 **解析 + 只读渲染**，删除富文本/源码编辑能力。
完整富文本实现保存在 `archive/full-rich-text` 分支。

## 保留的 API

- 解析：`ParseMarkdown`、`MarkdownDocument`、`MarkdownNode`、`ParseOptions`、`PlainText`
- 增量解析：`MarkdownParseSession`、`MarkdownEdit`、`MarkdownParseStats`
- 流式解析：`MarkdownStream`
- 渲染：`Markdown()`、`VirtualizedMarkdown()`、`MarkdownStyle`、`MarkdownOptions`、`MarkdownVirtualizationOptions`
- 事件：`MarkdownEvents::{LinkActivated, WikiLinkActivated, FootnoteActivated}`

## 删除的 API

| API | 替代 |
|---|---|
| `MarkdownEditor`、`MarkdownEditorOptions`、`MarkdownEditorEvents` | 宿主应用自行使用 `TextField`；库只渲染 `Markdown(document)` / `VirtualizedMarkdown` |
| `huxerui/markdown/editing.h` | 头文件已删除 |
| `MarkdownRichText`、`MarkdownRichTextRun`、`MarkdownTextMark` | 无；编辑模型由宿主或上游编辑器维护 |
| `MarkdownBlockEditSession` | 无 |
| `ApplyMarkdownCommand`、`MarkdownEditCommand` | 无 |
| `ParseOptions::source_ranges` | 无；已删除源码区间采集 |
| `MarkdownSourceRange`、`MarkdownDocument::TopLevelSourceRange`、`MarkdownDocument::SourceRange` | 无 |
| `examples/editor`（Writer） | 已删除；只读能力由 `examples/preview` 覆盖 |
| `experiments/rich_text_editor` | 已删除 |
| `.github/workflows/release-writer.yml` | 已删除；Writer/Android APK 产线下线 |

## 迁移方式

1. 把 `MarkdownEditor(value, options)` 改为由应用自己持有 `TextField`，显示使用
   `Markdown(ParseMarkdown(source, options), markdown_options)`。
   大文档可配合 `VirtualizedMarkdown`。
2. 删除对 `editing.h`、`MarkdownRichText`、`MarkdownBlockEditSession`、
   `ApplyMarkdownCommand` 的引用。
3. 删除对 `source_ranges`/`SourceRange` 的依赖；本版本不提供源码坐标映射。
4. 更新 HuxerUI 依赖：官方 0.4.0 即可，不再需要 `TextField::TextStyles` 或
   `TextParagraphLayout`。

## 版本与依赖

- 本库版本：`0.3.0`
- CI 固定官方 `HuxerUI/HuxerUI` 提交 `8d2bcea1`（0.4.0）
- 解析器单独构建：`-DHUXERUI_MARKDOWN_BUILD_VIEW=OFF`

历史 0.2 迁移说明仍保留在 [migration-0.2.md](migration-0.2.md)。
