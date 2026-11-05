# 块级原位编辑

`MarkdownEditor` 是库组件，Writer 是使用它的验收 demo。当前采用“聚焦块显示源码、
块内实时预览、失焦恢复排版”的方式，先实现原位编辑链路。它还不是完整 Typora 编辑器。

```cpp
auto value = UseState(TextEditingValue::FromText("# Hello\n\nStart writing."));
return MarkdownEditor(value, {
    .parse_options = {.math = true, .preserve_blank_lines = true},
    .live_preview = true,
}).On<MarkdownEditorEvents::Changed>([value](const TextEditingValue& next) mutable {
    value = next;
}).On<MarkdownEditorEvents::Error>([](const std::string& message) {
    // Present a rejected edit; the last valid source remains available.
});
```

组件拥有虚拟滚动，需要有界高度，不要再包垂直 ScrollView。`markdown_options` 复用已有样式、
自定义渲染；编辑视图不提供静态正文的跨块选择，`selectable` 不生效。`virtualization` 的估高、
缓存距离、滚动控制器与 VirtualizedMarkdown 一致。链接在可编辑块中用于进入编辑，不触发跳转；
只读结构中的链接仍可绑定 MarkdownEvents。自定义渲染器拥有自身的交互和焦点行为。

顶层段落和标题中，具有已验证源码区间的块可点击或 Tab 聚焦。编辑块的输入框保持挂载，
实时解析不会重建输入法会话。输入换行、改成标题、插入列表或删除整段均直接替换原源码区间；
布局骨架在本次编辑期间冻结，失焦或 Ctrl+Enter 后再按最终 AST 更新块结构。
组合输入尚未结束时 Ctrl+Enter 不结束块编辑。输入选区与 composition 均使用 SDK 的 UTF-16。

`Changed` 传递全篇源码和换算到全篇的选区/composition，调用方接收完整 TextEditingValue。
输入验证/大小限制失败发出 Error，保留上次有效输入。外部替换文本会结束当前块；改变解析选项
会重建编辑会话，这两种操作均要求先完成组合输入。库不保存文件。

普通块预览同步解析，无尾随防抖；源码模式 demo 的全篇旁栏仍是 120 ms 合并窗口。
块内预览单独解析当前片段，引用链接/脚注只使用片段内定义；结束编辑后全篇渲染恢复完整引用上下文。
解析、源码校验/拷贝仍含 O(N) 工作，虚拟化只减少挂载视图数量。

空文档提供一个完整源码输入框及实时预览，保持该输入框直到失焦，防止首字符就切换结构打断 IME。
滚动离屏时原生输入会话结束，已接受的内容保留；重新进入该块会创建新原生输入会话。

## 源码区间

`ParseOptions::source_ranges` 默认关闭；编辑会话自动开启。`TopLevelSourceRange(index)` 返回
可选的 `MarkdownSourceRange{start_byte, end_byte}`，是对应输入的 UTF-8 半开字节区间，包含 Markdown
标记、缩进及内部换行，排除末行 EOL，保留相邻块的分隔。BOM 不属于可编辑内容。

MD4C 没有完整块区间回调。当前从原始文本回调定位候选行，再用相同选项独立解析候选区间，
只有得到相同的单个 AST 块才暴露区间。Setext 标题包含下划线标记行。空标题、仅合成文本、
上下文相关引用或无法验证的内容可能返回 nullopt，不猜测替换位置。列表、代码块、表格、
空行节点及嵌套内容暂不提供区间。错误索引抛 out_of_range。

此选项增加片段验证解析工作，不修改 AST 语义；不是每个 inline 节点的源码映射、无损序列化器
或长期稳定块 ID。普通解析/流式无需开启。增量解析同时复制前缀区间并平移重解析尾部区间。

## 独立编辑会话

`MarkdownBlockEditSession` 位于 editing.h，依赖 HuxerUI 的 TextEditingValue；parser-only 构建不含它。

- `UpdateSource` 更新全篇源码；相同源码不结束活动块。
- `Document` / `Source` 始终对应当前已接受输入。
- `BeginEdit(index)` 使用当前 Document 的索引，仅接受有可编辑区间的块。
- `EditingValue` / `UpdateEdit` 接收块内 UTF-16 选区和完整组合输入状态。
- `ApplyCommand` 复用 ApplyMarkdownCommand，组合输入时拒绝格式命令。
- `LayoutDocument` / `LayoutSource` 在本次编辑期间冻结；`EndEdit` 完成后换成当前文档。

线程内串行使用。更新失败保留此前状态。区间之外的原始字节不通过 AST 重建，不改变引用定义、
换行拼写或其他块。控件内部对未变前缀/后缀复用虚拟行身份；它不是应用可持久保存的块 ID。

## 尚未提供

精确点击排版字形到源码位置的映射、隐藏标记的富文本输入、跨块选择/编辑、表格/嵌套块编辑、
结构化 Enter/Backspace 行为、统一撤销重做、离屏活动编辑器保活和任意结构修改下的像素滚动锚定。
当前撤销仅来自活动 TextField 实例，切块、模式切换、外部替换或离屏重挂载不构成统一历史。
