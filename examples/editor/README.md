# Markdown Writer：类 Typora 编辑体验开发 demo

这是第二个独立桌面 demo，入口 `src/app.cpp`，目标程序 `markdown_editor`。
现有 [Playground](../preview/README.md) 继续验证语法、扩展、流式和虚拟化；Writer 专门验证编辑体验。

当前是源码/阅读双模式的第一阶段，不是已完成的 Typora 式所见即所得编辑器。
界面会明确显示这个阶段。尚无点击段落原位编辑、源码/显示位置映射、跨模式统一撤销重做、
表格原位编辑或自动保存；关闭程序会丢弃草稿。

## 当前可以验证

- 默认阅读视图使用 VirtualizedMarkdown，点击「编辑源码」进入 TextField。
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

1. 源码区间、稳定块身份、显示位置与源码位置的映射。
2. 点击段落进入原位编辑、离开后回到排版，保持光标和中文输入法状态。
3. 统一编辑事务与撤销重做，覆盖工具栏、输入、结构变化和模式切换。
4. 跨块选择、表格编辑、虚拟化下的编辑焦点和滚动锚定。

这些能力应进入库的编辑层，demo 负责界面组合与验收。
