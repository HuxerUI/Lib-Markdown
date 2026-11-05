# 块级原位编辑

`MarkdownEditor` 是库组件，Writer 是它的验收 demo。默认对支持的段落/标题直接编辑显示文字，
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

默认 `rich_text=true` 时，具有保守源码区间且行内结构受支持的段落/标题使用富文本直编。
当前支持 Text、Strong、Emphasis、Underline、Strikethrough、Highlight、Insertion、Code、Link 和 WikiLink 的嵌套。
带图片、公式、脚注引用或剧透等暂未纳入编辑模型的行内结构回退为该块的源码输入。
列表、表格、代码块和嵌套块暂为只读，通过 demo 的「编辑源码」修改。

`rich_text=false` 对所有可定位的段落/标题使用源码输入；该方式允许改变 Markdown 结构并拆成多个块。
`live_preview` 只控制源码回退和空文档输入的下方预览；富文本直编始终显示排版，不额外生成重复预览。
源码片段预览使用片段内引用上下文；完成编辑后恢复全篇引用上下文。

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
单纯选择文字不会修改源码。块外的原始字节保留，不用 AST 重建全篇。
生成结果会重新交给 MD4C，并对比显示文字和语义格式；无法忠实表达的编辑被拒绝，不静默丢格式。
必要时用字符引用处理 Markdown 定界符的边界歧义；源码模式可以查看实际结果。

`MarkdownEditorEvents::Changed` 给出全篇源码及全篇 UTF-16 选区/composition，调用方接收完整值。
原始拼写尚未规范化时，少数源码与显示位置间的模式切换光标映射仍保守处理；当前块中的鼠标、
键盘、选区和 IME 使用显示文字的原生布局坐标。
输入验证、大小限制或无法序列化的操作发出 Error，保留上次有效内容；必要时结束并重建输入客户端，
避免显示未被文档接受的文本。此时可再次点击继续编辑。

当前富文本标题不能直接输入换行，行内代码也不能直接插入换行；这些操作会报错，结构修改使用源码模式。
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

MD4C 没有完整块区间回调。当前从原始文本回调定位候选行，再用相同选项独立解析，只有得到相同
单个 AST 块才暴露区间。Setext 标题包含标记行。空标题、合成文本、上下文相关引用或无法验证的
内容可能返回 nullopt。列表、代码、表格、空行和嵌套内容暂不提供区间。错误索引抛 out_of_range。

此选项增加验证解析工作，不改变 AST 语义，不是行内源码映射或长期稳定块 ID。
增量解析复制前缀区间并平移重解析尾部区间；验证片段的解析不计入主解析 parsed_bytes。

## 源码编辑会话

`MarkdownBlockEditSession` 接受源码及解析选项，依赖 HuxerUI 的 TextEditingValue，parser-only 不含它。
`Document` / `Source` 始终对应当前接受内容；`BeginEdit(index)` 只接受可定位块。
`EditingValue` / `UpdateEdit` 使用块内源码的 UTF-16 选区和完整 composition。
`ApplyCommand` 复用 ApplyMarkdownCommand，组合输入期间拒绝格式命令。
`LayoutDocument` / `LayoutSource` 在本次编辑期间冻结，`EndEdit` 后换成当前文档。
线程内串行使用；失败保留此前状态；外部替换源码、切块和解析配置变更须先完成组合输入。

后续设计问题记录在[编辑器后续讨论](editor-follow-up.md)，不作为当前实现的隐式承诺。
