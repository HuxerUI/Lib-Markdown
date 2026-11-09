# 重构计划：裁剪为 Markdown 阅读/解析库

状态：已执行（0.3.0，`refactor/read-only`）
基线：`main` @ `83e4b20`（裁剪前）
归档：`archive/full-rich-text` @ `83e4b20`（完整富文本/编辑能力已保留）
目标版本：`0.3.0`（破坏性 API 裁剪）

## 1. 目标与非目标

### 目标

- 库只提供 **Markdown 解析 + 只读渲染**：解析器、增量解析、流式解析、渲染 View、链接/脚注等事件。
- 删除源码编辑、富文本编辑、Writer demo、Android/桌面打包相关内容。
- 不再依赖 fork 的 `TextField::TextStyles` / `TextParagraphLayout`，CI 切回官方 HuxerUI 0.4.0。

### 非目标

- 不做新的编辑器/富文本实现；后续由 HuxerUI 作者维护。
- 不保留 Writer 应用与 APK 发布产线；如需只读 Android demo，另行立项。
- 不改变解析/渲染的行为语义（除删除 source range 采集外）。

## 2. 能力边界

### 保留

| 模块 | API / 文件 |
|---|---|
| 解析 | `ParseMarkdown`、`MarkdownDocument`、`MarkdownNode`、`ParseOptions`、`ParseOptions::CommonMark/GitHub`、`PlainText` |
| 增量解析 | `MarkdownParseSession`、`MarkdownEdit`、`MarkdownParseStats`、`ApplyEdit` |
| 流式解析 | `MarkdownStream` |
| 渲染 | `Markdown()`、`VirtualizedMarkdown()`、`MarkdownStyle`、`MarkdownOptions`、`MarkdownVirtualizationOptions` |
| 事件 | `MarkdownEvents::{LinkActivated, WikiLinkActivated, FootnoteActivated}` |
| 扩展 | md4c、表格/任务列表/警告块/脚注/公式/剧透/上下标等解析与渲染 |

### 移除

| 模块 | 符号 / 文件 |
|---|---|
| 编辑器 View | `MarkdownEditor`、`MarkdownEditorOptions`、`MarkdownEditorEvents` |
| 编辑模型 | `MarkdownRichText`、`MarkdownRichTextRun`、`MarkdownTextMark`、`MarkdownBlockEditSession` |
| 源码命令 | `ApplyMarkdownCommand`、`MarkdownEditCommand` |
| 公共头 | `include/huxerui/markdown/editing.h` |
| Source range | `ParseOptions::source_ranges`、`MarkdownSourceRange`、`MarkdownDocument::TopLevelSourceRange/SourceRange`（推荐一并移除；如保留则保留 `source_range_test`） |
| 示例 | `examples/editor/**`、`experiments/rich_text_editor/**` |
| 发布 | `.github/workflows/release-writer.yml`（Writer/APK 产线） |

## 3. 文件级变更清单

### 删除

```
include/huxerui/markdown/editing.h
src/editing.cpp
src/editing_internal.h
src/rich_text.cpp
src/rich_text_model.cpp
src/rich_text_model.h
src/rich_text_editor.cpp
src/rich_text_editor.h
src/document_editor_internal.cpp
src/document_editor_internal.h

tests/editing_test.cpp
tests/rich_text_test.cpp
tests/block_editor_view_test.cpp
tests/rich_editor_view_test.cpp
tests/source_range_test.cpp        # source range 一并裁剪时

examples/editor/**                  # Writer + Android/Linux shell + keystore + tests
experiments/rich_text_editor/**

.github/workflows/release-writer.yml
docs/block-editing.md
docs/editor-follow-up.md
docs/plans/rich-text-editor-refactor.md
```

### 修改

| 文件 | 变更 |
|---|---|
| `include/huxerui/markdown.h` | 删除 `MarkdownEditorOptions/Events/MarkdownEditor` 段；清理不再使用的 include |
| `include/huxerui/markdown/document.h` | 删除 source range API/字段；删除编辑类 friend 声明；保留 `MarkdownParseSession`、`MarkdownStream` |
| `src/document.cpp` | 删除 source range 采集、`BlockSource`、`TopLevelSourceRange/SourceRange` |
| `src/markdown.cpp` | 删除编辑半区（`BlockEditorState`、`DocumentEditorState`、`EditableField`、`MarkdownEditor` 等）；保留 Renderer/Virtual*/`Markdown`/`VirtualizedMarkdown` |
| `src/inline.h` | 保留 `BuildInline`、`OriginalLink`、`InlineResult`；删除 `BuildEditableStyles`、`DocumentEditablePaths`、`RenderEditableBlock` |
| `CMakeLists.txt` | 移除编辑源文件；删除 `add_subdirectory(examples/editor)`；版本 0.2.4 → 0.3.0 |
| `tests/CMakeLists.txt` | 移除编辑/富文本/source-range 测试目标 |
| `tests/view_test.cpp` | 删除 `CheckEditableInlineStyles` 及调用；保留渲染断言 |
| `tests/api_examples.cpp` | 删除 `FormatSelection`、`BlockEditor` 等编辑示例 |
| `.github/workflows/ci.yml` | 移除 editor 矩阵与 rich-text-editor job；pin 改为官方 HuxerUI 0.4.0 |
| `README.md`、`README.zh-CN.md` | 重写能力清单、构建要求、示例说明；删除 Writer/编辑/Android 段 |
| `examples/preview/README.md` 等 | 如引用 Writer/编辑能力则同步修改 |
| `docs/math-and-editing.md` | 保留公式/空行章节，删除编辑命令章节，可重命名为 `docs/math.md` |
| `CHANGELOG.md` | 新增 0.3.0 条目，列出移除的 API 与迁移路径 |
| `docs/migration-0.3.md` | 新增：从 0.2.x 迁移到 0.3.0（保留解析/渲染，删除编辑 API） |

### 保留不动

- `third_party/md4c/**`
- `examples/preview/**`（解析 + 只读渲染 demo）
- `cmake/HuxerUIDependency.cmake`（Android 仍要求 static 组件，保留 static 分支）
- `HuxerUIProject.cmake`、库 target/别名
- `docs/incremental-parsing.md`、`docs/streaming-and-virtualization.md`、`docs/md4c-extensions.md`
- 历史迁移文档 `docs/migration-0.2.md`

## 4. SDK 与 CI 调整

- 官方 SDK pin：`HuxerUI/HuxerUI` @ `36f93958`（0.4.0；官方尚无 v0.4.0 tag）。
- 删除 fork pin `FarnaHerry/HuxerUI@062f3092` 及所有 `TextStyles`/`TextParagraphLayout` 说明。
- `ci.yml`：
  - parser 矩阵（Linux/Windows/macOS）保持。
  - native 矩阵只保留 `library`、`preview`；删除 `editor`。
  - 删除 `rich-text-editor` job。
- 删除 `release-writer.yml`：Writer/Android APK 发布线随 demo 一起下线。
  - 如后续需要只读预览发布，另建 `release-preview.yml`。
- 本地验证优先使用已安装官方 SDK：`HUXERUI_HOME=$HOME/.local/share/HuxerUI`（0.4.0）。

## 5. 执行阶段

### Phase 0：冻结（已完成）

- `main` 提交 `83e4b20`。
- 创建 `archive/full-rich-text` 指向 `83e4b20`。
- 后续裁剪在 `refactor/read-only` 工作分支执行，验证通过后合并 `main`。

### Phase 1：公共 API 裁剪

1. 删除 `editing.h`。
2. 从 `markdown.h` 删除编辑器 API。
3. 从 `document.h` 删除 source range API 与编辑 friend。
4. 保持 parser/stream/incremental/render API 不变。

验证：`grep -rn "MarkdownEditor\|MarkdownRichText\|MarkdownBlockEditSession\|ApplyMarkdownCommand\|editing.h" include` 无结果。

### Phase 2：实现与构建裁剪

1. 删除编辑实现文件。
2. 从 `markdown.cpp` 删除编辑半区，保留渲染。
3. 精简 `inline.h`。
4. 更新 `CMakeLists.txt` 源列表。
5. 删除编辑测试并更新 `tests/CMakeLists.txt`、`view_test.cpp`、`api_examples.cpp`。

验证：

```sh
cmake -S . -B build-parser -DHUXERUI_MARKDOWN_BUILD_VIEW=OFF -DHUXERUI_MARKDOWN_BUILD_TESTS=ON
cmake --build build-parser --parallel
ctest --test-dir build-parser --output-on-failure
```

### Phase 3：示例与文档

1. 删除 `examples/editor/**`、`experiments/rich_text_editor/**`。
2. CMake 只保留 `examples/preview`。
3. 删除编辑相关文档，重写 README/预览说明。
4. 新增 `docs/migration-0.3.md`、CHANGELOG 0.3.0。

验证：`examples/preview` 独立配置、构建、测试通过。

### Phase 4：SDK 与 CI

1. pin 切到 `HuxerUI/HuxerUI@36f93958`。
2. 移除 editor/rich-text CI 任务。
3. 删除 Writer release workflow。
4. 版本号 0.3.0。

验证：YAML 解析通过；本地 source/installed 两种 SDK 模式各构建一次。

### Phase 5：全量验证

- 官方 SDK 0.4.0（installed）：
  `cmake -S . -B build -DHUXERUI_HOME=$HOME/.local/share/HuxerUI -DHUXERUI_MARKDOWN_BUILD_TESTS=ON -DHUXERUI_MARKDOWN_BUILD_EXAMPLES=ON`
- 官方 SDK 0.4.0（source）：CI 同款 `-DHUXERUI_HOME=<checkout>`。
- parser-only + view 两套测试全部通过。
- `grep` 确认无 fork API、无编辑符号残留（迁移文档除外）。
- CI workflow YAML 校验。

### Phase 6：合并与收尾

- 合并 `refactor/read-only` 到 `main`。
- 更新 CHANGELOG、版本号，打 tag 前确认 `archive/full-rich-text` 保留。
- 归档分支不删除，至少保留一个发布周期。

## 6. 验收标准

- 库只暴露解析 + 只读渲染 API；无 `MarkdownEditor`/`editing.h`。
- parser-only 构建不依赖 HuxerUI；view 构建只依赖官方 0.4.0 API。
- 官方 SDK source/installed 两种方式均可构建、测试。
- `examples/preview` 可独立构建运行。
- CI 绿：parser 三平台、native library/preview。
- 仓库内不再出现 `TextField::TextStyles`、`TextParagraphLayout`、fork pin。
- 发布流程不再产出 Writer/Android APK。

## 7. 风险与开放决策

| 项 | 风险 | 建议 |
|---|---|---|
| Source range 移除 | 破坏 `ParseOptions`/`MarkdownDocument` API；未来源码定位需重新加入 | 推荐移除；若下游需要，保留 API 但停止内部使用 |
| 语义化版本 | 0.3.0 破坏公共 API | 在 CHANGELOG/migration 中明确 |
| 预览 demo 覆盖 | 只读预览已覆盖主要渲染能力 | 保留 `examples/preview`，删除 Writer |
| 发布产线 | 删除 Writer release 后无二进制发布 | 需要时另建 preview release workflow |
| 归档分支 | 后续富文本维护在上游 | `archive/full-rich-text` 只读保留 |
