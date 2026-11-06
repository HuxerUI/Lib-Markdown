# 块级原位编辑

`MarkdownEditor` 是库组件，Writer 是它的验收 demo。默认对支持的段落、标题和表格单元格直接编辑显示文字，
保留标题大小、粗体、斜体、下划线、删除线、高亮、插入、行内代码和链接样式。
编辑时不显示 Markdown 定界符，不在下方重复显示同一段；文字、命中测试、光标、选区和输入法
共用 HuxerUI 的同一份 attributed paragraph。

```cpp
auto value = UseState(TextEditingValue::FromText("# Hello\n\nA **bold** paragraph."));
return MarkdownEditor(value, {
    .parse_options = {.math = true, .preserve_blank_lines = true},
    .live_preview = true,
    .rich_text = true,
}).On<MarkdownEditorEvents::Changed>([value](const TextEditingValue& next) mutable {
    value = next;
}).On<MarkdownEditorEvents::Error>([](const std::string& message) {
    // Show a rejected edit; the previous accepted source remains available.
});
```

需要提供 `TextField::TextStyles` 的 HuxerUI；当前验证版本与构建方式见根 README。
组件拥有虚拟滚动，需要有界高度，不要再包垂直 ScrollView。`markdown_options` 复用已有样式。
编辑视图不提供静态正文的跨块选择，`selectable` 不生效。`virtualization` 与 VirtualizedMarkdown 一致。
编辑块中的链接用于定位光标，修改的是链接标签，目标保留；只读结构中的链接仍可绑定 MarkdownEvents。

## 两种块内编辑方式

默认 `rich_text=true` 时，具有源码区间且行内结构受支持的段落、标题和单元格使用富文本直编。
当前支持 Text、Strong、Emphasis、Underline、Strikethrough、Highlight、Insertion、Code、Link 和 WikiLink 的嵌套。
列表项、引用、提示块和脚注定义中的正文复用同一个编辑器。列表标记、任务标记、引用容器和表格对齐随原结构保留。
显式空单元格也可以点击输入。表格使用 Tab 切换焦点，Enter 完成当前格。

图片、公式、脚注引用或剧透可走对应块的源码输入。代码、含硬换行的嵌套段落、带合成单元格的表格可打开经验证的外层块源码。
当源码区间因上下文无法验证时，可用 Writer 的「编辑源码」修改。
`rich_text=false` 优先打开经验证的整个顶层块，允许改成多个 Markdown 块。
`live_preview` 控制源码输入的下方预览。富文本直编本身显示排版。片段预览使用局部引用上下文，完成编辑后恢复全篇上下文。

点击或 Tab 可聚焦编辑；点击离开或 Ctrl+Enter 完成。实时输入不等待停下输入。
组合输入未结束时 Ctrl+Enter 不结束块。活动块的布局身份保持稳定，滚动离屏后原生输入会话结束，
已接受的内容保留；重新进入该块会建立新会话。空文档暂时提供完整源码输入及预览，失焦后按块显示。

## 富文本模型与源码回写

`MarkdownRichText` 位于 editing.h，提供显示文字 `Value`、带语义标记的 UTF-16 `Runs`、
`Update` 与生成 Markdown 源码及选区的 `SourceValue`。可以独立于 demo 使用。

编辑显示文字时，替换内容继承第一个被替换字符的格式；空选区插入继承前一个字符，段首使用首字符格式。
先使用旧选区、输入法组合范围或删除后的光标定位修改，再回退到 Unicode 标量边界的前后缀差异，
避免相邻相同字符导致修改落入错误格式区。输入法取消会恢复组合开始时的格式。

首次内容输入或接受组合输入时，当前块的 Markdown 可以规范化，例如定界符、转义、标题空格和字符引用。
嵌套段落中的普通软换行可以规范为一行，保留段落的可见文字和外层容器。
单纯选择文字不会修改源码。块外的原始字节保留，不用 AST 重建全篇。
生成结果会重新交给 MD4C，并对比显示文字和语义格式；无法忠实表达的编辑被拒绝，不静默丢格式。
必要时用字符引用处理 Markdown 定界符的边界歧义；源码模式可以查看实际结果。

`MarkdownEditorEvents::Changed` 给出全篇源码及全篇 UTF-16 选区/composition，调用方接收完整值。
原始拼写尚未规范化时，少数源码与显示位置间的模式切换光标映射仍保守处理；当前块中的鼠标、
键盘、选区和 IME 使用显示文字的原生布局坐标。
输入验证、大小限制或无法序列化的操作发出 Error，保留上次有效内容；必要时结束并重建输入客户端，
避免显示未被文档接受的文本。此时可再次点击继续编辑。

富文本标题、嵌套正文和行内代码中的结构换行使用源码模式。
普通段落的单个换行可序列化为硬换行；新增块和结构化 Enter/Backspace 尚待独立事务模型。
富文本中输入 Markdown 标记当前按字面字符处理，不自动执行 Markdown 快捷转换。

原生 TextField 管理当前实例的文本撤销。富文本模型另保留最多 32 个、总量 2 MiB 的最近语义快照，
支持原生撤销回到这些显示文字时恢复格式。该缓存不是跨块或跨模式的统一历史。
切块、模式切换、离屏重挂载及复杂格式事务的统一撤销重做仍待实现。

解析、校验、源码拷贝和当前块的显示模型更新仍有 O(N) 工作；虚拟化只减少挂载视图数量。
此版本不承诺长单段落的低延迟，也没有文件保存。

## 保守源码区间

`ParseOptions::source_ranges` 默认关闭，编辑会话自动开启。
`TopLevelSourceRange(index)` 返回可选 `MarkdownSourceRange{start_byte,end_byte}`，坐标是对应输入的
UTF-8 半开字节区间，包含标记、缩进和内部换行，排除末行 EOL；BOM 不属于可编辑内容。

内置 MD4C 的位置观察回调提供容器起始行、叶节点和单元格的字节区间。Setext 标题和围栏代码区间涵盖结束标记。
顶层候选区间用相同选项独立复核语法树，区间边缘的显式空行留在原处。上下文相关引用或合成内容可能没有区间。
`SourceRange(path)` 使用从根开始的子节点索引。嵌套段落、单行标题和实际表格单元格的区间从正文开始，
多行段落区间可能包含中间的引用前缀与缩进。索引错误抛 `out_of_range`。

此选项增加验证解析工作，不改变 AST 语义，不是行内源码映射或长期稳定块 ID。
增量解析复制前缀区间并平移重解析尾部区间；验证片段的解析不计入主解析 parsed_bytes。

## 源码编辑会话

`MarkdownBlockEditSession` 接受源码及解析选项，依赖 HuxerUI 的 TextEditingValue，parser-only 不含它。
`Document` / `Source` 对应当前接受内容。`BeginEdit(index)` 打开顶层块，`BeginEdit(path)` 可以打开嵌套正文或单元格。
嵌套替换写入单行 Markdown，并在提交前验证原容器和相邻节点。表格校验会检查整个输入确实属于当前单元格。
`EditingValue` / `UpdateEdit` 使用块内源码的 UTF-16 选区和完整 composition。
`ApplyCommand` 复用 ApplyMarkdownCommand，组合输入期间拒绝格式命令。
`LayoutDocument` / `LayoutSource` 在本次编辑期间冻结，`EndEdit` 后换成当前文档。
线程内串行使用；失败保留此前状态；外部替换源码、切块和解析配置变更须先完成组合输入。

后续设计问题记录在[编辑器后续讨论](editor-follow-up.md)，不作为当前实现的隐式承诺。
