# 公式语法与空行

本库使用未修改的内置 MD4C。提供公式 AST、独占公式渲染接入点与显式空行显示；
不接入 TeX 排版引擎，也不提供编辑模型。

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
