# Markdown Playground

可独立构建的 HuxerUI 桌面 demo，展示 `HuxerUI::Markdown` 的公开接口。
支持 Windows、macOS 和 Linux；原生运行效果已在 Windows 验证。

## 功能

- 五组样例：基础排版、列表与表格、链接与边界、自定义渲染、长文档。
- 左侧可编辑 Markdown，点击「渲染」更新右侧预览，点击「重置样例」恢复原文。
- 明暗主题切换、隐藏/显示源码；窄窗口自动变成上下布局。
- 原生文本选择和复制；相对地址、中文地址和锚点通过底部状态栏反馈。
- 自定义代码卡片展示语言和行号；自定义图片回调绘制本地 Canvas。

编辑器用于演示完整受控输入，预览在点击「渲染」后更新；它不是增量 Markdown
编辑器。链接只显示回调，不会打开浏览器。图片示例仅识别 `demo:landscape`，
不请求网络。组件默认渲染行为可以通过「自定义渲染」开关对比。

demo 使用 0.2 API：State 保存 `MarkdownDocument` 值，通过
`.On<MarkdownEvents::LinkActivated>()` 处理链接，使用 `code_block_renderer` 和
`standalone_image_renderer` 自定义显示。新文档的滚动重置使用 demo 自己的版本号，
主题切换复用已有快照。升级旧接入代码请参考 [迁移说明](../../docs/migration-0.2.md)。

## 独立构建

需要 CMake 3.20+、C99/C++20 编译器，以及带富文本接口的 HuxerUI SDK 或源码。
可从本仓库根目录运行：

```sh
cmake -S examples/preview -B build-preview -DHUXERUI_HOME=/path/to/HuxerUI
cmake --build build-preview --config Release --parallel
```

Windows 使用 Visual Studio 生成器时，运行：

```powershell
.\build-preview\Release\markdown_preview.exe
```

Linux/Ninja 的程序通常位于 `build-preview/markdown_preview`；macOS 的输出为
`build-preview/markdown_preview.app`（多配置生成器会增加 `Release` 目录）。
Windows/Linux 分发时必须保留可执行文件旁的 `markdown_preview.resources` 目录；
macOS 资源放在 app bundle 内。

也可以在本目录执行 `cmake -S . -B build -DHUXERUI_HOME=...`，不需要先配置库的根项目。

## 从库根项目构建

```sh
cmake -S . -B build -DHUXERUI_HOME=/path/to/HuxerUI -DHUXERUI_MARKDOWN_BUILD_EXAMPLES=ON
cmake --build build --config Release --target markdown_preview --parallel
```

Windows/Visual Studio 输出位于 `build/examples/preview/Release/markdown_preview.exe`。

## 交互测试

HuxerUI 源码提供的无窗口测试库可以验证样例切换、编辑并渲染、重置、链接回调、
主题切换、自定义渲染，以及窄窗口布局：

```sh
cmake -S examples/preview -B build-preview -DHUXERUI_HOME=/path/to/HuxerUI \
  -DHUXERUI_BUILD_TESTING_LIBRARY=ON -DHUXERUI_MARKDOWN_PREVIEW_BUILD_TESTS=ON
cmake --build build-preview --config Release --parallel
ctest --test-dir build-preview -C Release --output-on-failure
```

CI 会独立构建并测试 demo，在成功的 `native (preview)` 作业中上传
`markdown-preview-windows` artifact，包含 exe 和资源目录。

## 代码位置

- [src/app.cpp](src/app.cpp)：编辑与预览界面、主题、链接回调和自定义渲染。
- [src/samples.h](src/samples.h)：五组 Markdown 样例。
- [platform/desktop/main.cpp](platform/desktop/main.cpp)：桌面启动入口。
- [tests/preview_test.cpp](tests/preview_test.cpp)：使用真实应用界面的交互测试。

---

This standalone desktop demo uses the library's public API. Choose one of five
samples, edit its Markdown, and click Render to update the preview. It supports
light/dark themes, a compact stacked layout, link callbacks, and custom code and
image renderers. Links and images do not initiate network requests. Use the CMake
commands above with a compatible HuxerUI SDK/source tree; distribute the executable
with its sibling resource directory (or the complete macOS app bundle).
