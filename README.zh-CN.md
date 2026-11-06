# Lib-Markdown

[English](README.md) · HuxerUI 原生 Markdown 扩展，使用 C++20 和 MD4C。

0.2 包含源代码不兼容的 API 调整，升级请参考 [迁移说明](docs/migration-0.2.md)。

支持 CommonMark 标题、段落、嵌套列表、引用、分隔线、代码块，以及 GFM
表格、删除线、任务列表和自动链接。文本由 HuxerUI `AttributedText` 渲染，
支持选中复制、Unicode、明暗主题和链接回调。
可选启用高亮、插入文本、下划线、Wiki 链接、提示块和脚注。

## 接入

```cmake
# 在 huxerui_add_app(...) 之后：
huxerui_use_library(your_app
    TARGET HuxerUI::Markdown
    PATH "/path/to/Lib-Markdown")
```

```cpp
#include <huxerui/huxerui.h>
#include <huxerui/markdown.h>

using namespace huxerui;
using namespace huxerui::markdown;

View Article() {
  return ScrollView(Markdown("# 你好\n\n这是 **Markdown**。")
    .On<MarkdownEvents::LinkActivated>([](const MarkdownLinkActivation& event) {
      // 由应用决定如何处理 event.destination。
    }));
}
```

长文档可以先在工作线程调用 `ParseMarkdown(source)`，保留返回的
`MarkdownDocument` 值句柄，再在 UI 中传给 `Markdown(document)`。复制共享只读存储，
相等比较按快照身份判断；默认构造是有效空文档，移动后原对象仍有效。
通过 `document.Root()` 读取节点，借用节点期间保留快照所有者。
这样重组界面时不必重复解析。输入字符串不需要持续存活。

## 增量解析

连续编辑时保留一个会话，将返回的不可变快照交给 `Markdown(document)`：

```cpp
MarkdownParseSession parser;
auto document = parser.Update(source);
document = parser.ApplyEdit({.start_byte = parser.Source().size(),
                             .inserted_text = "\n新增段落。\n"});
auto stats = parser.LastParseStats();
// 编辑器提供完整源码时，也可以调用 parser.Update(next_source)。
```

编辑范围以 UTF-8 字节计数，必须落在 Unicode 标量边界，不能直接使用 UI 的 UTF-16
选区偏移。会话复用安全边界之前的块并重解析尾部；引用定义或无法确认安全边界时全量解析。
相同输入保留快照身份；失败保留上次成功的源码、快照和统计。会话同步执行，需串行访问。
源码扫描和前部 AST 复制仍为线性开销。虚拟化由独立视图提供。
详见 [增量解析契约](docs/incremental-parsing.md)。

## 流式追加与虚拟化

```cpp
MarkdownStream stream;
auto document = stream.Append(chunk); // 接受任意字节分片，包括拆开的 UTF-8 字符。
// 后续按顺序 Append；将快照发布到 UI State。
document = stream.Finish();

// 自带垂直滚动，需要有界高度，不再外套 ScrollView。
return VirtualizedMarkdown(document, {}, {
  .estimated_block_extent = 80.0F,
  .cache_extent = 400.0F,
}).With(Frame{.height = 600.0F});
```

流式接口暂存未完整的 UTF-8 字符，遇到非法输入保持旧状态；`Finish()` 检查截断字符并结束输入。
已有完整 UTF-8 文本时，也可直接用 `MarkdownParseSession::Append(text)`。
后续分片会重新解释未闭合结构与引用定义，仍使用正常 Markdown 语义。

`VirtualizedMarkdown` 按顶层块测量高度并按需挂载，支持样式、链接和离屏全文复制；可通过
`scroll_controller` 按顶层块索引定位。尾部追加保留阅读位置，视口上方插入暂不保证像素锚定。
单个大型列表、表格或代码块内部不拆分，AST 和文本仍完整保存在内存中。
详见 [流式与虚拟化契约](docs/streaming-and-virtualization.md)。

## MD4C 可选扩展

```cpp
auto document = ParseMarkdown(source, {
  .highlight = true, .insertions = true, .underline = true,
  .wiki_links = true, .admonitions = true, .footnotes = true,
});
return Markdown(document)
  .On<MarkdownEvents::WikiLinkActivated>([](const MarkdownWikiLinkActivation& event) {
    // 由应用处理 event.target 页面标识，不自动作为 URI 打开。
  })
  .On<MarkdownEvents::FootnoteActivated>([](const MarkdownFootnoteActivation& event) {
    // 由应用根据 event.number / event.label 定位脚注。
  });
```

六项扩展默认关闭；`==高亮==`、`++新增++`、`_下划线_` 直接显示原生样式。
启用下划线会改变下划线强调语义，斜体/粗体请用星号。
`[[页面|标签]]` 产生独立 Wiki 事件，`> [!NOTE]` 等提示块使用主题容器，
可通过 `admonition_renderer` 自定义。脚注按首次引用顺序编号，显示引用与文末定义，
点击产生独立事件，不自动滚动。流式、增量和虚拟化均适用。
完整支持范围和后续顺序见 [MD4C 能力矩阵](docs/md4c-extensions.md)。

第二阶段可选 `math = true` 识别公式正文，默认按等宽源码显示；
`standalone_math_renderer` 可接入独占段落公式的自定义显示，混排行内公式仍为源码回退。
`preserve_blank_lines = true` 保留 BlankLines/count，使用 `blank_line_height` 控制间距并保留
正文间复制换行。两项默认关闭，不代表已完成公式排版或源码无损往返。
详见 [公式、空行与编辑基础](docs/math-and-editing.md)。

## 配置

- `ParseOptions`：传给 `ParseMarkdown`，分别控制表格、删除线、任务列表、自动链接和换行规则。
- `style`：省略时跟随外层 HuxerUI 主题；可从 `MarkdownStyle::FromTheme(UseTheme())` 派生完整局部样式。
- `selectable`：默认启用文本选择与复制。
- `MarkdownEvents::LinkActivated`：通过 `.On<...>()` 绑定，接收包含原始地址的事件；组件不自动导航。
- `link_policy`：覆盖默认的 HTTP(S)、mailto 和相对地址策略，允许应用自定义协议。
- `code_block_renderer`：仅处理代码块，接收 `text`、`language` 和 `fence_info`，可接入语法高亮。
- `standalone_image_renderer`：仅处理独占段落图片；默认和行内图片显示替代文字。

源码重载使用默认解析选项；自定义解析时显式准备文档：

```cpp
auto document = ParseMarkdown(source, {.soft_break_mode = SoftBreakMode::LineBreak});
return Markdown(document);
```

renderer 在 UI 组合时执行，节点引用仅在回调期间借用，延迟使用时复制需要的数据。
节点可能是渲染层拥有的副本，仅保留输入文档不能延长回调引用的生命周期。
事件处理器在重组后更新，组件卸载后停止通知；事件引用需要保留时也应复制。
`IsDefaultLinkAllowed()` 只表达默认允许策略，不代表完整 URI 校验或导航安全保证。

原始 HTML 按文字显示。默认输入上限为 8 MiB、嵌套上限为 128，超过限制会抛出
`std::length_error`；无效 UTF-8 会抛出 `std::invalid_argument`。

任务复选框只读；表格使用固定列宽并支持横向滚动；默认代码块为可换行的等宽文本。
组件本身不包含完整可视化编辑器、数学排版或 HTML 执行。增量解析由独立会话提供。

## 交互 Demo

[Markdown Playground](examples/preview/README.md) 提供源码编辑与渲染对照、七组样例、
明暗主题、链接反馈和自定义代码/图片渲染。默认按 120 ms 窗口合并输入并刷新预览，
持续输入也会更新，可关闭后点击
「渲染」手动更新；底部显示重解析字节和复用块数。窄窗口自动上下排列。
「虚拟化」切换按需挂载；「流式重放」每 40 ms 追加最多 48 字节，可随时停止，编辑或切换样例也会取消重放。
「MD4C 扩展」样例开启六项可选语法，状态栏显示 Wiki 与脚注事件。
「公式与空行」展示源码回退和自定义公式接口，不是公式排版引擎。

```sh
cmake -S examples/preview -B build-preview -DHUXERUI_HOME=/path/to/HuxerUI
cmake --build build-preview --config Release --parallel
```

Windows/Visual Studio 下运行 `build-preview/Release/markdown_preview.exe`，
并保留旁边的 `markdown_preview.resources` 目录。CI 会提供 Windows demo artifact。

第二个 [Markdown Writer](examples/editor/README.md) 位于 `examples/editor`，是类 Typora
体验的实验版。默认使用库的 `MarkdownEditor`，可直接编辑排版后的段落、标题、列表项、引用和表格单元格。
文字、光标和输入法共用原生富文本布局。代码块和复杂内容使用原位源码输入，结构修改使用全文源码模式。
具体支持范围和后续任务见[块级编辑接口](docs/block-editing.md)和[后续记录](docs/editor-follow-up.md)。

```powershell
cmake -S examples/editor -B build-editor -DHUXERUI_HOME=/path/to/HuxerUI
cmake --build build-editor --config Release --parallel
.\build-editor\Release\markdown_editor.exe
```

根项目开启 examples 会同时构建两个 demo，Writer 输出为 `build/examples/editor/Release/markdown_editor.exe`。

## 构建

需要 CMake 3.20+、C99/C++20 编译器和提供 `TextField::TextStyles` 的 HuxerUI SDK 或源码。
已验证版本为 `e29c3c2573dcd7db201e7523ff59803529875d52`，位于 SDK 的 `feat/text-field-attributed-styles` 分支；CI 固定该提交。

```sh
cmake -S . -B build -DHUXERUI_HOME=/path/to/HuxerUI \
  -DHUXERUI_MARKDOWN_BUILD_TESTS=ON \
  -DHUXERUI_BUILD_TESTING_LIBRARY=ON \
  -DHUXERUI_MARKDOWN_BUILD_EXAMPLES=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

示例程序为 `markdown_preview`，包含中文、嵌套样式、列表、表格和主题切换。
只需要解析器时设置 `-DHUXERUI_MARKDOWN_BUILD_VIEW=OFF`，不需要 HuxerUI。

项目与内置 MD4C 均采用 MIT 许可证，详见 [英文说明](README.md)。
