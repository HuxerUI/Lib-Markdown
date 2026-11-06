# Markdown Writer：类 Typora 编辑体验开发 demo

这是第二个独立 demo，入口 `src/app.cpp`，目标程序 `markdown_editor`。
现有 [Playground](../preview/README.md) 继续验证语法、扩展、流式和虚拟化；Writer 专门验证编辑体验。

点击支持的段落、标题、列表项、引用或表格单元格即可保持排版直接编辑。输入时保留粗体、斜体和行内代码等样式。
光标、选区和中文输入法共用原生富文本布局。结构修改通过「编辑源码」完成。草稿保存在本次运行的内存中。

## 当前可以验证

- 自定义窗口标题栏中的文档标签可新增、切换和关闭；每个标签保留独立草稿、选区和跨段编辑会话。
- 顶部快捷工具行集中提供格式、编辑模式、实时预览和主题操作；正文占据剩余空间。
- 默认使用库的 MarkdownEditor。直接点击正文或单元格，也可用 Tab 聚焦。列表符号、序号、任务勾选和表格对齐随结构保留。
- 显式留空的表格单元格可以点击输入。单元格文字可折行，按 Enter 完成当前格。
- 图片、公式、脚注引用、剧透等未纳入富文本模型的行内内容暂回退为块内源码输入及预览。
- 代码块、含硬换行的嵌套段落和缺少显式单元格的表格可打开对应块的源码输入。上下文相关且无法定位的内容通过「编辑源码」修改。
- 使用源码输入把段落改成多段或列表，完成编辑后刷新整页块结构。
- 点击「编辑源码」进入整个文档的 TextField，点击「原位编辑」返回。
- 源码模式可选实时预览，连续输入按 120 ms 窗口合并，不要求停下输入。
- 工具栏支持粗体、斜体、行内代码、一级/二级标题、列表和引用。
- 工具栏调用库的 ApplyMarkdownCommand，不在 demo 内另写一套文本格式算法。
- 完整接受 TextEditingValue，保存 UTF-16 选区与输入法 composition；活动组合输入期间不执行格式命令。
- 触控工具栏在焦点切换前记住选区，源码版本未变才使用它，避免把格式插到文末或使用过期范围。
- 模式切换前立即解析当前草稿，取消旧任务；返回源码模式保留文本与选区值。
- 明暗主题、窄窗口上下排列、解析错误时保留旧预览，链接只在状态栏反馈。

块内编辑和源码模式各用当前 TextField 实例的原生编辑历史。跨段模式持有自己的文档事务，详见下文。
工具栏点击后可再次聚焦源码继续输入。

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
推送 `v*` 标签时，`Release Markdown Writer` 工作流构建 Windows ZIP、macOS app ZIP、Linux 绿色 tar.gz、DEB、RPM，以及分别适用于 ARM64 设备和 x86_64（AMD64）设备的 Android APK，并附加到同名 GitHub Release；手动运行工作流只生成 Actions 构建产物。
Linux 包依赖系统提供 GTK4、libepoxy 和 libsoup 3；DEB/RPM 安装后可在应用菜单中找到 Markdown Writer。Windows、macOS 包保留各自的资源目录或 app bundle。Android APK 使用调试签名，适合安装验证 demo；正式分发需要稳定的发布签名。
工具栏的“打开文件”通过系统文件选择器读取 Markdown 或纯文本文件，并在新标签页中打开；文件允许写入时可点击“保存”写回。源码模式按回车增删行时会立即刷新预览。

## 跨段编辑实验

带段落几何接口的构建可在默认样例打开「跨段编辑」。相邻的标题和正文可以跨段拖选、输入中文、用 Enter 拆段及 Backspace 合段。列表项、引用、提示块、脚注正文和显式表格单元格可在各自文字范围内输入，沿用原有排版。`Ctrl+Z` 和 `Ctrl+Y` 同时还原或重做源码和选区。

选中内容后可从跨段工具栏切换粗体、斜体和行内代码。编辑面聚焦时也支持 `Ctrl+B` 和 `Ctrl+I`。工具栏操作和输入共用撤销历史。

草稿在两种编辑方式和源码视图间共享。切换视图后，返回「跨段编辑」可继续原有撤销历史。期间的源码或块内修改合为一次导入。跨多个嵌套项的替换、嵌套结构调整、代码块和复杂行内内容可通过块内或源码模式处理。顶层空白行按正文段落间距显示，新建空段落在当前跨段会话中可输入。

从库根目录构建此模式。以下命令将两项尚在 SDK 分支上的改动组合在独立的本地 SDK checkout 中。

```powershell
git clone https://github.com/FarnaHerry/HuxerUI.git .cache/writer-sdk
git -C .cache/writer-sdk checkout --detach e119529d4fe118b0b4df4a0a306df6819814d7a0
git -C .cache/writer-sdk fetch https://github.com/HuxerUI/HuxerUI.git e29c3c2573dcd7db201e7523ff59803529875d52
git -C .cache/writer-sdk cherry-pick --no-commit e29c3c2573dcd7db201e7523ff59803529875d52
cmake -S examples/editor -B build-writer-experiment -DHUXERUI_HOME="$PWD/.cache/writer-sdk" -DHUXERUI_MARKDOWN_EDITOR_DOCUMENT_MODE=ON
cmake --build build-writer-experiment --config Release --parallel
.\build-writer-experiment\Release\markdown_editor.exe
```

## 测试和后续阶段

独立配置增加 `-DHUXERUI_BUILD_TESTING_LIBRARY=ON -DHUXERUI_MARKDOWN_EDITOR_BUILD_TESTS=ON`，
构建后运行 `ctest --test-dir build-editor -C Release --output-on-failure`。
测试覆盖默认样例的嵌套任务编辑、Unicode 选区格式化、持续输入、模式切换、草稿保留、主题与窄窗口。
CI 的独立 editor 作业还覆盖跨段编辑和多标签草稿隔离，产物为 `markdown-editor-windows`。

后续按这些依赖顺序推进：

1. 补齐原始拼写尚未规范化时的源码模式光标转换，增加结构化 Enter/Backspace。
2. 统一编辑事务与撤销重做，覆盖工具栏、输入、结构变化和模式切换。
3. 跨块选择、表格行列操作及更完整的虚拟编辑滚动锚定。

源码区间、MarkdownBlockEditSession、MarkdownRichText 和 MarkdownEditor 均在库中，demo 负责组合与验收。
需要 `TextField::TextStyles` SDK，版本见根 README。具体边界见[块级编辑接口](../../docs/block-editing.md)，
待讨论事项见[后续记录](../../docs/editor-follow-up.md)。
