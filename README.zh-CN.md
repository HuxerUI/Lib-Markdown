# Lib-Markdown

[English](README.md) · HuxerUI 原生 Markdown 扩展，使用 C++20 和 MD4C。

支持 CommonMark 标题、段落、嵌套列表、引用、分隔线、代码块，以及 GFM
表格、删除线、任务列表和自动链接。文本由 HuxerUI `AttributedText` 渲染，
支持选中复制、Unicode、明暗主题和链接回调。

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
  return ScrollView(Markdown("# 你好\n\n这是 **Markdown**。", {
    .on_link = [](const std::string& destination) {
      // 由应用决定如何处理链接。
    },
  }));
}
```

长文档可以先在工作线程调用 `ParseMarkdown(source)`，保留返回的
`std::shared_ptr<const MarkdownDocument>`，再在 UI 中传给 `Markdown(document)`。
这样重组界面时不必重复解析。输入字符串不需要持续存活。

## 配置

- `parse`：分别控制表格、删除线、任务列表、自动链接和换行规则。
- `theme`：省略时跟随外层 HuxerUI 主题；可以从 `MarkdownTheme::FromTheme(UseTheme())` 修改。
- `selectable`：默认启用文本选择与复制。
- `on_link`：接收原始解码后的地址，包括相对路径和锚点；组件不会自动打开链接。
- `link_policy`：覆盖默认的 HTTP(S)、mailto 和相对地址策略，允许应用自定义协议。
- `code_renderer`：接收代码文本、语言和完整 fence 信息，可接入语法高亮。
- `image_renderer`：自定义独占段落图片的显示与加载；默认和行内图片显示替代文字。

原始 HTML 按文字显示。默认输入上限为 8 MiB、嵌套上限为 128，超过限制会抛出
`std::length_error`；无效 UTF-8 会抛出 `std::invalid_argument`。

任务复选框只读；表格使用固定列宽并支持横向滚动；默认代码块为可换行的等宽文本。
当前版本不包含编辑器、增量解析、虚拟化、数学公式或 HTML 执行。

## 交互 Demo

[Markdown Playground](examples/preview/README.md) 提供源码编辑与渲染对照、五组样例、
明暗主题、链接反馈和自定义代码/图片渲染。编辑后点击「渲染」，窄窗口自动上下排列。

```sh
cmake -S examples/preview -B build-preview -DHUXERUI_HOME=/path/to/HuxerUI
cmake --build build-preview --config Release --parallel
```

Windows/Visual Studio 下运行 `build-preview/Release/markdown_preview.exe`，
并保留旁边的 `markdown_preview.resources` 目录。CI 会提供 Windows demo artifact。

## 构建

需要 CMake 3.20+、C99/C++20 编译器和带富文本接口的近期 HuxerUI SDK 或源码。

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
