# 公式语法、空行与编辑基础

本阶段继续使用未修改的内置 MD4C。新增公式 AST 和独占公式渲染接入点、显式空行显示，
以及供第二个 demo 使用的源码格式命令。未接入 TeX 排版引擎或完整富文本编辑器模型。

## 公式接入

```cpp
auto document = ParseMarkdown(source, {.math = true});
return Markdown(document, {.standalone_math_renderer = [](const MarkdownNode& math) -> View {
  // InlineMath 或 DisplayMath；math.text 为不含定界符的原始 LaTeX。
  return Text(math.text); // 源码显示示例，可替换为实际公式后端。
}});
```

math 默认 false，映射 MD_FLAG_LATEXMATHSPANS。InlineMath 对应单美元符号，DisplayMath 对应
双美元符号；后者仍是 MD4C 的 span，不会强制拆成独立顶层块。节点拥有 literal text，保留
LaTeX 反斜杠、实体拼写和内容换行，不在公式内部继续解析 Markdown 标记。PlainText 提取公式正文。

默认显示等宽源码，不做数学排版。standalone_math_renderer 仅接收“Paragraph 的唯一子节点”
形式的公式，两类 math 都适用；与其他文字混排、标题或表格中的公式仍用源码回退。回调在 UI
组合期间借用节点，滚动/重组可重复调用；它不属于解析器线程，也不会接到任意段落片段。
选择/复制的逻辑文字仍是 LaTeX 正文，自定义 View 不自动绑定选择端点。

流式未闭合的美元标记按 MD4C 普通文本显示，后续闭合后可改变 AST；Finish 不补定界符。
不验证 LaTeX 语法，不加载外部公式服务，不声称上下标、分数或根号已经排版。

## 保留空行

preserve_blank_lines 默认 false，映射 MD_FLAG_PRESERVEBLANKLINES。
开启后，BlankLines 节点的 blank_line_count 保存 MD4C 报告的空行数量；不是字节范围。
缩进、行结束符、原始标记和所有空白拼写并未保留，所以不能用它承诺源码无损往返。

MarkdownStyle::blank_line_height 是每个空行的非负有限 DIP 高度。显式空行替代相邻的自动
block_spacing，避免重复叠加；没有 BlankLines 的文档维持原布局规则。
虚拟列表对零高项使用 0.001 DIP 的最小测量高度，避免 SDK 忽略零值后保留旧估高。
PlainText 包含对应换行；普通/虚拟视图均使用逻辑选择分隔符保留正文之间的空行。
没有文字端点的开头和末尾空白不参与 Ctrl+A/C 导出；需要确定文本请使用 PlainText。
代码块内部的空行留在 CodeBlock.text，不另建 BlankLines。

增量重启仍只使用 MD4C 确认的未变标题/段落边界，新空白节点参与快照比较；含引用/脚注定义
仍全量回退。新增测试按四种 math/空行配置进行 2,400 次随机编辑，并逐字节流式对照全量 AST。

## 可复用格式命令

```cpp
// value 为 TextField 的完整受控值，选区单位是 UTF-16，不是解析会话的 UTF-8 字节。
auto next = ApplyMarkdownCommand(value, MarkdownEditCommand::Bold);
```

该接口位于 `<huxerui/markdown/editing.h>`，也由主头引入；只在视图库构建中提供。
操作是纯值转换，源对象保持不变，不进行解析、不更改焦点，也不管理历史。

- Bold/Italic 包裹选区；空选区在空标记中间放置光标。不自动识别或撤销已有格式。
- InlineCode 为包含反引号的内容选择更长定界符，并按需填充空格；拒绝多行选区。
- Heading1/Heading2 替换现有 ATX 前缀；BulletList/BlockQuote 添加行前缀，同一前缀不重复添加。
- 块命令处理选中行，末端恰在下一行起点时不包括下一行。保留 CRLF、选区方向和 affinity。
- 无效 UTF-8、越界/拆开 surrogate 的选区、非法命令抛 invalid_argument。
  活动 IME composition 抛 logic_error，调用方先完成组合输入。不会静默丢弃 provisional text。

它是 Markdown 源码操作，不是所见即所得事务；不保证包裹任意复杂 Markdown 片段都能形成
期望的嵌套格式。需要 AST 编辑、源码映射、撤销合并等能力时继续建设独立编辑层。

## 两个 demo

- [Playground](../examples/preview/README.md)：七组能力样例，新增公式/空行和源码渲染卡片。
- [Writer](../examples/editor/README.md)：独立 `markdown_editor`，段落/标题原位编辑、实时预览及全篇源码工具栏。

两个 demo 都复用本库解析与渲染。Writer 使用库的 MarkdownEditor 与 MarkdownRichText，支持段落/标题
保持排版直接修改，字形、光标和 IME 共用原生 attributed layout；跨块/跨模式统一事务仍待实现。
具体边界见[块级编辑](block-editing.md)与[后续讨论](editor-follow-up.md)。
