# 流式追加与顶层块虚拟化

两项能力相互独立，也可以组合：`MarkdownStream` 产生不可变 `MarkdownDocument`，
UI 把最新快照交给 `VirtualizedMarkdown`。普通 `Markdown` 与一次性解析行为不变。

## 流式输入

```cpp
MarkdownStream stream;
auto first = stream.Append("# Hello\n\n\xF0\x9F"); // 暂存半个 emoji。
auto next = stream.Append("\x98\x80\n");
auto final = stream.Finish();
```

- `Append` 按到达顺序接收原始 UTF-8 字节，不要求分片在字符边界。结尾未完整的标量暂存，
  最多三字节，不传给解析器；`Source()` 仅包含已提交的完整 UTF-8，`PendingByteCount()` 返回待补全字节数。
- 不完整分片可以改变 pending 状态，但不产生新文档、不改变解析统计；空分片同样不解析。
- 可确认非法的序列立即抛 `invalid_argument`，包括非法首字节、continuation、过长编码、
  surrogate 和大于 U+10FFFF 的值，不使用替换字符掩盖网络解码错误。
- 输入限制包含已提交、暂存和新分片；超限抛 `length_error`。深度限制沿用 `ParseOptions`。
  验证、解析或分配失败均不提交任何新状态；调用方可修正分片后重试。
- `Finish()` 在 pending 非空时抛 `invalid_argument`，保留会话以便继续补字节。成功后标记
  `IsFinished()`，重复 Finish 返回同一快照，后续任何 Append 抛 `logic_error`。重播请创建新实例。
- Finish 不自动补反引号、围栏、括号或标签。每次展示都是当前完整 UTF-8 源码的正常解析结果；
  后续分片可关闭结构，引用定义可以反向影响前文并触发全量回退。
- `Document()` 借用当前句柄；复制快照可跨更新/销毁保留。`Source()` 是借用视图。
  会话不可复制/移动，同步执行，使用单线程或外部串行化。

已经完整解码的文本可用 `MarkdownParseSession::Append(text)`，效果相当于在当前源码末尾
调用 ApplyEdit。该方法不缓冲不完整 UTF-8。所有解析仍遵循[增量解析的开销和回退规则](incremental-parsing.md)。

网络、超时、取消、线程调度与 UI 发布节奏由应用控制。不要把一个流交给并行 worker 同时 Append；
不要每接收一个字节就重新布局 UI，可以按帧或短窗口合并发布快照。

## 虚拟视图

```cpp
auto controller = UseScrollController();
return VirtualizedMarkdown(document, {.selectable = true}, {
  .estimated_block_extent = 80.0F,
  .cache_extent = 400.0F,
  .scroll_controller = controller,
}).On<MarkdownEvents::LinkActivated>([](const MarkdownLinkActivation& event) {
  // 使用 event.destination 导航。
}).With(Frame{.height = 600.0F});
```

视图自带垂直滚动，必须放在有界高度的布局中，不要外套纵向 ScrollView。
`estimated_block_extent` 为正有限 DIP 初始估高，实际高度由 SDK 测量修正；`cache_extent`
为非负有限 DIP 离屏缓存距离。配置非法时抛 `invalid_argument`。
控制器的 `ScrollToItem(index)` 使用顶层 AST 块索引，不是章节号、行号或文字选择块索引。

每个顶层块对应一个虚拟项。创建项声明时先返回延迟 Scope，只有被实现的 Scope 才调用
renderer 和创建 Text；自定义回调可以在滚出后再次滚入时重新执行，不保证调用次数。
嵌套列表、引用、表格和单个代码块内部仍整体组合。表格保留自己的横向滚动。

更新按 AST 内容比较未变化的前缀和后缀，复用这些顶层块的记录、key 和文字身份；
中间变化的块得到新身份。重复内容无法代表业务上的唯一块身份，不承诺任意重排的精确匹配。
尾部追加保留已有阅读位置，主题变化沿用块记录；在视口上方插入/删除或改变大块高度时，
底层列表按逻辑索引维护测量锚点，暂不保证原文字的像素位置不变。应用可用控制器重新定位。
不自动追随流式末尾，也不自动跳转 Markdown 锚点。

## 离屏选择和复制

开启 `selectable` 时使用 SDK 的 `TextSelectionSource` 提供完整逻辑文本，把标题、段落、代码和
表格单元格绑定到唯一 TextBlockId。列表符号是装饰文字；复制时文字块用换行分隔，代码原有
换行保留，不导出 Markdown 标记。可先点击正文，再 Ctrl+A / Ctrl+C 复制包含离屏内容的全文，
无需挂载所有视图。滚动离屏不丢失逻辑选区。

未变化的前后块沿用选择 ID；选区端点所在块被替换/移除会清除受影响选区。追加不会自动扩展
已存在的选区，重新全选才包含新块。自定义代码/图片 renderer 的任意 Text 没有自动绑定：
它们的源码文字仍可参与跨区域/全文复制，但自定义显示不能作为该逻辑选区的端点。
需要完全自定义的选择交互时，可关闭 selectable 并由应用负责。

## 成本与验证

虚拟化降低挂载、测量和绘制的视图数量，不会释放离屏 AST。当前实现保留完整文档，并为顶层
渲染记录保存一份节点与逻辑纯文本；变化时共享未变记录，不为复用的每块保留一整份历史文档。
快照比较和选择位置索引仍有 O(文档大小) 的成本。没有把整条链路变成 O(可见项) 或 O(新增字节)。

`markdown.stream` 覆盖每个两分片切点、逐字节/随机分片、Unicode、BOM、NUL、换行、引用定义、
代码/表格/列表、配置组合、异常不提交、输入/深度限制和 Finish 生命周期，并逐次对照全量解析。
`markdown.virtualization` 用 900 个顶层块验证有限 Text 挂载、离屏全文复制不触发额外渲染、
滚动、链接、尾部更新/主题保持、未变块身份、选择端点保留、宽度变化、空文档和嵌套渲染。
demo 交互测试验证重放完成、停止、用户编辑/切换样例取消，以及虚拟列表内的增量更新。
