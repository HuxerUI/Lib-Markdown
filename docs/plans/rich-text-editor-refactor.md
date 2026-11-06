# RichTextEditor 与 Markdown 编辑重构计划

日期 2026-11-06。以当前 Writer 的块内富文本编辑、源码回退和虚拟滚动作为验收基线。

## 目标

先在库内验证可独立使用的 `RichTextEditor`，以 Writer 作为首个实际使用场景。通用控件负责
段落编辑、跨段选区、输入法和撤销。`MarkdownEditor` 将编辑事务转换为源码补丁，Markdown 源码
是它的权威数据。文档中的段落以 `AttributedText` 快照交给原生布局，复用当前显示链路。

Markdown 编辑 API 可以随模型迁移调整，届时同时迁移 Writer、使用示例和测试。上游 SDK 接口
以独立编辑器的通用需求为依据。[HuxerUI 段落与选择设计](https://github.com/HuxerUI/HuxerUI/blob/a6e00e4dac3861b13c5018653dd1835238bcd3b4/docs/design/text.md#scope-and-ownership)

## 借鉴官方 streaming_text 示例

- `TranscriptIndex` 和稳定 `TextBlockId` 负责段落顺序与定位，独立的 `State<AttributedText>` 负责活动段落。
  内容更新可以复用索引和已完成的块。阶段 3 采用这一分层，验证编辑一段时可复用其他段落快照，滚动离屏后选区仍能定位。
  [段落索引与发布](https://github.com/HuxerUI/HuxerUI/blob/a6e00e4dac3861b13c5018653dd1835238bcd3b4/examples/streaming_text/main.cpp#L178)
- 示例让 `VirtualList` 和 `SelectionArea::Source` 读取同一批段落 ID，已卸载的段落也能参与复制。
  Lib-Markdown 的只读视图已经使用这套协议，`RichTextEditor` 将编辑端点和事务放在独立于视图生命周期的文档状态里。
  [虚拟列表与逻辑选区](https://github.com/HuxerUI/HuxerUI/blob/a6e00e4dac3861b13c5018653dd1835238bcd3b4/examples/streaming_text/main.cpp#L1077)
- 流式发布按批刷新，后台高亮用块 ID 和版本拒绝过期结果，自动跟随取决于用户是否在末尾。
  阶段 4 按 Markdown 解析结果确定受影响的段落，新增引用定义时允许刷新前文。以末尾追加、引用定义回改前文和滚动离开末尾分别验收。
  [批量刷新与版本检查](https://github.com/HuxerUI/HuxerUI/blob/a6e00e4dac3861b13c5018653dd1835238bcd3b4/examples/streaming_text/main.cpp#L533)

## 阶段 1　统一行内样式映射

已完成。Windows Release 主构建 19 项测试及独立 Writer 的交互测试均通过。样式对照覆盖默认与自定义链接策略。

- 预览和 Writer 共用 Markdown 标记到 `TextSpanStyle` 的转换，保留当前段落基准样式、链接目标和源码范围。
- 两种视图用同一链接策略决定普通链接的颜色和下划线。预览可触发链接事件，编辑态点击定位光标。
- 用中文、emoji、嵌套格式、代码、高亮和链接对比两条路径的 UTF-16 样式范围。运行原有块内编辑和预览回归。

## 阶段 2　验证 HuxerUI 段落几何接口

已推送 `codex/paragraph-layout`。Windows 公共头文件、通用与原生测试通过。Fedora 通用测试和 16 项原生文字布局用例通过，文件选择器及 EGL 相关的 9 项失败与官方基线一致。[SDK 提交](https://github.com/FarnaHerry/HuxerUI/commit/e119529d4fe118b0b4df4a0a306df6819814d7a0)

- 从官方 `main` 验证一个最小的公开布局查询接口。输入是 `AttributedText`、基准样式、可用宽度与布局选项。
  对外提供点击命中、光标矩形和范围矩形，统一使用段落局部逻辑坐标、UTF-16 偏移与 affinity。
- 用实际原生段落布局对照绘制、换行和选区，覆盖混合字重、窄列、组合字符及双向文本。
  单独验证 SDK 通用测试及原生后端。将通用查询作为独立的上游改动准备审查。
- 继续使用现有 `TextInputClient` / `TextSelectionClient` 协议接入平台输入和选区。编辑器侧负责将
  段落局部坐标映射到文档、视口和平台输入位置。

## 阶段 3　实现独立 RichTextEditor 的首条编辑路径

独立原型位于 [experiments/rich_text_editor](../../experiments/rich_text_editor/README.md)。每段使用 `Text(AttributedText)` 显示，编辑面持有单个原生输入客户端。文档模型处理原子输入批次、跨段替换和统一撤销。链接外观在编辑态投影为样式，正文鼠标操作交给编辑面。

Windows 已通过模型和交互套件，并验证打包示例可启动。组合输入回归使用平台归一化指令，真实输入法候选窗与辅助功能留在设备验收。

- 用带稳定标识的段落快照、文档选区和一个输入会话表达整篇编辑状态。输入、跨段替换、格式操作和
  撤销使用同一套事务。输入法预编辑按一次提交分组，取消时恢复先前的内容和选区。
- 先验证纯富文本小示例。从一段拖选到另一段，用中文输入法替换，一次撤销恢复文字和选区。
  再验证跨段加粗、键盘移动、复制粘贴和滚动离屏后的选区。示例以 HuxerUI 和通用编辑器为依赖。
- 只有成功的事务才发布新段落快照。渲染视图按需挂载，文档历史和编辑会话由编辑器持有。

## 阶段 4　迁移 MarkdownEditor 与 Writer

### 4A　接入标题和正文

Writer 提供可选的跨段编辑模式。标题与普通段落共用 `Text(AttributedText)` 渲染和一个 `TextInputClient`。
编辑事务将 UTF-16 选区映射为 UTF-8 源码补丁，复用增量解析会话并校验相邻语法块。Enter 新建正文段落，
跨段删除合并到第一个块。空段落在活动会话中保留编辑位置。组合输入、撤销和重做保存源码与编辑选区快照。

测试覆盖跨标题和正文拖选、归一化中文输入指令、取消组合、一次撤销和重做、CRLF 拆合段、空段落以及相邻字节保留。
跨段模式以独立构建选项启用，默认编辑和复杂 Markdown 的编辑仍使用已有块内路径。切换模式会重建输入会话。

### 4B　迁移行内格式命令

跨段模式的粗体、斜体和行内代码使用同一个文档事务。命令按显示选区切分语义样式，
保留段间字节，验证生成的 Markdown，再发布源码和选区。撤销和重做保留选区方向。
在同一选区再次执行已有格式会移除该格式。组合输入结束后可执行格式命令。

### 4C　接入混合文档中的可编辑文字

跨段模式复用 Markdown 的列表、引用、提示块、脚注和表格排版。虚拟列表按顶层块挂载，块内可定位的文字共用文档输入会话。
写回嵌套文字时使用已验证的子块源码区间，校验整个容器语法和相邻块。不可定位或暂未支持的内容仍可通过块内或源码模式编辑。

### 4D　保留跨段会话并导入源码修改

Writer 持有文档会话，切到块内或源码模式再返回时可继续撤销和重做。其他模式中的修改在返回时
以一次导入写入跨段历史。撤销导入恢复前一份草稿和显示选区。源码输入仍有自身的原生编辑历史。

### 4E　迁移嵌套结构命令与选区映射

- 接入嵌套结构的 Enter/Backspace，并在源码和跨段模式间转换选区。继续用增量解析和稳定块标识支撑虚拟滚动。
- 每批运行解析器、视图库、Playground 和 Writer 回归。设备验收覆盖 IME 候选窗、鼠标拖选、
  高 DPI、辅助功能以及右对齐单元格末尾光标。

## 阶段 5　准备上游组件评审

纯富文本示例与 Writer 都使用同一套编辑核心后，整理通用控件、公开契约和跨平台测试给 HuxerUI 审查。
Markdown 解析、源文档适配和 Writer 保持为库层消费方。原生上下标和行内附件分别在实际需求出现时
以共享排版能力设计及验收。
