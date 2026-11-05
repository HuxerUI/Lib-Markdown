# Markdown Writer：类 Typora 编辑体验开发 demo

这是第二个独立桌面 demo，入口 `src/app.cpp`，目标程序 `markdown_editor`。
现有 [Playground](../preview/README.md) 继续验证语法、扩展、流式和虚拟化；Writer 专门验证编辑体验。

当前对支持的段落/标题保持排版直接编辑，隐藏 `#`、`**` 等标记；输入时保留原有字号、粗体、
斜体、行内代码等样式，不重复显示下方预览。光标、选区和中文输入法共用原生富文本布局。
尚无跨块/跨模式统一撤销重做、表格原位编辑、结构化 Enter 或自动保存；关闭程序会丢弃草稿。
富文本标题/行内代码暂不接受换行，结构修改通过「编辑源码」完成。

## 当前可以验证

- 默认使用库的 MarkdownEditor，段落/标题可以一次点击进入原位编辑，也可 Tab 聚焦。
- 支持的段落直接编辑显示文字并保留格式，输入框保持挂载和输入法会话。
- 图片、公式、脚注引用、剧透等未纳入富文本模型的行内内容暂回退为块内源码输入及预览。
- 仅在源码输入方式中，段落可以改成多段、列表等结构，结束编辑后更新整页块结构。
- 列表、表格、代码块、嵌套段落和无法确认源码区间的块暂为只读，通过「编辑源码」修改。
- 点击「编辑源码」进入整个文档的 TextField，点击「原位编辑」返回。
- 源码模式可选实时预览，连续输入按 120 ms 窗口合并，不要求停下输入。
- 工具栏支持粗体、斜体、行内代码、一级/二级标题、列表和引用。
- 工具栏调用库的 ApplyMarkdownCommand，不在 demo 内另写一套文本格式算法。
- 完整接受 TextEditingValue，保存 UTF-16 选区与输入法 composition；活动组合输入期间不执行格式命令。
- 触控工具栏在焦点切换前记住选区，源码版本未变才使用它，避免把格式插到文末或使用过期范围。
- 模式切换前立即解析当前草稿，取消旧任务；返回源码模式保留文本与选区值。
- 明暗主题、窄窗口上下排列、解析错误时保留旧预览，链接只在状态栏反馈。

SDK TextField 自身的编辑历史仅限当前挂载实例；切换模式或工具栏直接替换受控值不构成统一
事务历史。本阶段不承诺这些操作可跨模式 Undo。工具栏点击后可再次聚焦源码继续输入。

## 本地启动

从库根目录构建两个 demo：

```powershell
cmake -S . -B build -DHUXERUI_HOME=/path/to/HuxerUI -DHUXERUI_MARKDOWN_BUILD_EXAMPLES=ON
cmake --build build --config Release --parallel
.\build\examples\editor\Release\markdown_editor.exe
```

只构建 Writer：

```powershell
cmake -S examples/editor -B build-editor -DHUXERUI_HOME=/path/to/HuxerUI
cmake --build build-editor --config Release --parallel
.\build-editor\Release\markdown_editor.exe
```

Windows/Linux 分发时保留 exe 旁的 `markdown_editor.resources`。macOS 分发整个 app bundle。
Linux 单配置输出通常是 `build-editor/markdown_editor`，macOS 是 `markdown_editor.app`。

## 测试和后续阶段

独立配置增加 `-DHUXERUI_BUILD_TESTING_LIBRARY=ON -DHUXERUI_MARKDOWN_EDITOR_BUILD_TESTS=ON`，
构建后运行 `ctest --test-dir build-editor -C Release --output-on-failure`。
测试实际界面的 Unicode 选区格式化、持续输入、模式切换即时刷新、草稿保留、主题与窄窗口。
CI 增加独立 editor 作业及 `markdown-editor-windows` 产物。

后续按这些依赖顺序推进：

1. 扩大源码区间覆盖，补齐原始拼写尚未规范化时的源码模式光标转换，增加结构化 Enter/Backspace。
2. 统一编辑事务与撤销重做，覆盖工具栏、输入、结构变化和模式切换。
3. 嵌套块、跨块选择、表格编辑及更完整的虚拟编辑滚动锚定。

源码区间、MarkdownBlockEditSession、MarkdownRichText 和 MarkdownEditor 均在库中，demo 负责组合与验收。
需要 `TextField::TextStyles` SDK，版本见根 README。具体边界见[块级编辑接口](../../docs/block-editing.md)，
待讨论事项见[后续记录](../../docs/editor-follow-up.md)。
