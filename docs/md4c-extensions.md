# MD4C 能力接入与接口约定

本轮按“行内样式 → 页面链接 → 容器 → 全局脚注”的顺序接入六项已有能力。
沿用内置 MD4C `f9e5eb232b95b737373ae706648cb75bd259ed1a`，第三方源码不变。
公式排版、可编辑文档模型等后续项没有预留空接口。

## 接口依据

参考 HuxerUI 原组织的公开接口：

- [Lib-CodeEditor](https://github.com/HuxerUI/Lib-CodeEditor/blob/main/include/huxerui/codeeditor.h)：
  EditorOptions 值配置、EditorEvents 类型化事件、PascalCase 符号与 snake_case 字段。
  本地同名仓库是另一个分支，本轮另行读取原组织头文件核对，不将本地分支作为唯一依据。
- [Lib-QRCode](https://github.com/HuxerUI/Lib-QRCode)：编码选项与显示样式分离、值类型选项与相等比较。
- 同级 Lib-Map / Lib-PDF 的公开头：独立 Options/Style、明确默认值、坐标/生命周期契约与应用负责交互决策。

ParseOptions 仅配置解析；MarkdownOptions/MarkdownStyle 仅配置显示，不透出 MD4C 原始位掩码。
新增 ParseOptions 值相等比较。NodeKind 数值有变化，不能作为持久化协议；应用需重新编译并更新
穷举 visitor。不为了保留旧二进制兼容而增加别名。

## 已接入能力

| 选项 | MD4C 开关 | AST / 默认显示 |
| --- | --- | --- |
| highlight | MD_FLAG_HIGHLIGHT | Highlight；`==text==` 使用高亮前景/背景 |
| insertions | MD_FLAG_INSERT | Insertion；`++text++` 使用下划线 |
| underline | MD_FLAG_UNDERLINE | Underline；下划线替代 `_` / `__` 的强调用途 |
| wiki_links | MD_FLAG_WIKILINKS | WikiLink；`[[target]]` 或带标签形式，保留目标与标签 |
| admonitions | MD_FLAG_ADMONITIONS | Admonition；Note/Tip/Important/Warning/Caution 容器 |
| footnotes | MD_FLAG_FOOTNOTES | FootnoteReference、FootnoteSection、FootnoteDefinition |

六项默认均为 false；原有 tables/strikethrough/task_lists 保持 true，自动链接的 URL/email/WWW 三类默认均开启。
MD4C 原生优先级决定语法嵌套、转义、围栏内字面内容和格式组合，不用字符串正则替换标记。
启用 underline 后，请用 `*` 和 `**` 输入斜体与粗体。

```cpp
ParseOptions parse{
  .highlight = true, .insertions = true, .underline = true,
  .wiki_links = true, .admonitions = true, .footnotes = true,
};
MarkdownParseSession session(parse);
auto document = session.Update(source);
// MarkdownStream stream(parse) 使用同样配置；Markdown / VirtualizedMarkdown 接收快照。
```

默认解析不启用这些扩展。示例程序的 DemoParseOptions 显式开启六项及后续新增的 math/空行选项；这不是库的隐式方言切换。
HTML 仍按字面文字显示，不启用 MD4C HTML 输出，不接入网络下载。

## 后续接入的解析选项

```cpp
auto parse = ParseOptions::GitHub(); // tables/strike/task/autolinks/admonitions/footnotes
parse.spoilers = true;
parse.autolinks = {.urls = true, .emails = false, .www = false};
parse.permissive_atx_headings = true;
parse.indented_code_blocks = false;
parse.collapse_whitespace = true;
```

`ParseOptions::CommonMark()` 关闭 GFM 扩展和裸文本自动链接；两种预设的 HTML 均按字面显示。
预设只是普通 ParseOptions 值，可以继续逐项修改。默认构造仍维持原来的基础 GFM 行为，
不会隐式开启 GitHub 预设中的提示块和脚注。

`MarkdownAutolinkOptions` 分别控制裸 URL、email 和 WWW；不影响显式链接或 `<https://...>`。
这是开发阶段的破坏性 API 更新，旧 `.autolinks = false` 改为 `{false, false, false}`。
宽松标题接受 `#heading`；禁用缩进代码后围栏代码仍工作；普通空白折叠保留代码、公式和实体。
当前 MD4C 的 COLLAPSEWHITESPACE 标记存在制表符分隔丢失，故由适配层折叠普通文本，
第三方源码未改动。默认三个选项均不改变原语法行为。

`spoilers=true` 识别 `||text||` 并生成 Spoiler 节点。普通/虚拟视图使用 `MarkdownStyle::spoiler_cover`
遮盖，点击、键盘链接激活或辅助功能 Activate 可独立揭示；揭示后内部链接恢复正常事件。
揭示状态保存在挂载段落中，内容变化或卸载后重置。复制和辅助功能仍可读取正文；这是视觉隐藏。
包含剧透的 Writer 段落暂使用源码回退，普通 Playground 的扩展样例可验证阅读揭示。

## 六项扩展的显示与交互

MarkdownStyle 新增 highlight_foreground/highlight_background 和
admonition_foreground/admonition_background。FromTheme 从语义主题色派生。
插入文本与下划线使用原生 TextDecoration，保留其他强调、删除线与链接样式的组合。

默认提示块显示英文类型标题和内容，五种类型共用可配置的提示配色；类型由标题区分。
MarkdownOptions::admonition_renderer 可整体替换容器，负责自定义标题、图标、配色和本地化。
回调仅在 UI 组合时借用节点，滚动或重组可再次调用；延迟使用需要复制数据。

WikiLink 使用 node.destination 存放实体解码后的目标，node.children 保存标签。
目标是应用页面标识，不经过 URI 解析或 link_policy；即使含冒号，也只发 WikiLinkActivated：

```cpp
return Markdown(document)
    .On<MarkdownEvents::WikiLinkActivated>([](const MarkdownWikiLinkActivation& event) {
      // 用 event.target 查找页面；不应未经应用策略校验就当网址打开。
    })
    .On<MarkdownEvents::FootnoteActivated>([](const MarkdownFootnoteActivation& event) {
      // event.number、event.label、event.reference；按应用策略定位。
    });
```

普通链接仍只发 LinkActivated 并应用 link_policy。新事件不同时触发普通链接事件。
所有事件沿用原有重组替换、卸载断连和借用 payload 契约；组件不自动导航或滚动。

## 脚注与快照

脚注按首次引用顺序从 1 编号，未引用定义不输出。节点保留：

- footnote_number：定义与引用共同编号；不是稳定业务 ID。
- footnote_label：实体解码后的标签；不是源码范围，修改源码不能用它代替位置映射。
- footnote_reference：同一脚注的第几次引用，从 1 开始。
- footnote_reference_count：定义的总引用次数。

引用默认显示可点击的 `[n]`，定义在文末区域带 `[n]` 装饰编号。PlainText 的引用为 `[n]`，
定义贡献正文；不额外生成装饰编号或提示块标题。MD4C 的直接行内脚注正文先归整为段落，
避免普通渲染拆成多个文字块，或虚拟化遗漏文字选择绑定。

定义形式包含 `]:`。旧/新源码有该标记时整个文档重解析，避免前文编号过期和重复拼接脚注区。
这也适用于流式末尾出现定义、删除定义或改变引用顺序。没有全局定义时仍可复用安全前缀。
整体 CPU/内存复杂度仍遵循原来的线性扫描和 AST 复制限制。

## 虚拟化与选择

新增容器的段落使用相同逻辑文字源，离屏复制不需要挂载所有视图。虚拟模式的提示标题、
脚注定义编号属于装饰文字，与列表符号一样不纳入逻辑全文复制；引用 `[n]` 属于正文，保留。
普通自动收集模式可能包含装饰 Text，调用方若需要确定的纯文本应使用 document.PlainText()。
自定义提示块内容不自动绑定选择端点，但其原始正文仍可参与逻辑跨块/全文复制。
整个脚注区是一个顶层块，内部不会继续虚拟化。

## 能力清点与后续顺序

依据 [官方 README](https://github.com/mity/md4c#markdown-extensions) 和
[固定版本头文件](https://github.com/mity/md4c/blob/f9e5eb232b95b737373ae706648cb75bd259ed1a/src/md4c.h)：

| 能力 | 当前状态 / 下一步 |
| --- | --- |
| CommonMark、GFM 表格/任务/删除线/自动链接 | 已接入 |
| highlight / insert / underline / Wiki / 提示块 / 脚注 | 本轮完成 |
| 普通换行显示为硬换行 | 已通过 SoftBreakMode 接入，无需暴露原始 MD4C flag |
| 公式语法（LATEXMATHSPANS） | 已接 AST、等宽源码回退、独占公式自定义渲染接口；完整公式排版后端仍待实现 |
| 上下标（SUPERSCRIPTS/SUBSCRIPTS） | SDK 当前 TextSpanStyle 没有基线偏移，需补底层排版能力再承诺显示支持 |
| 剧透（SPOILERS） | 已接 Spoiler AST、视觉遮盖、独立点击/键盘/辅助功能揭示；正文仍可复制 |
| 空行保留（PRESERVEBLANKLINES） | 已接 BlankLines/count、布局和正文间复制分隔符；不代表源码无损往返 |
| 宽松标题、禁用缩进代码、折叠空白 | 已提供独立 ParseOptions，默认行为不变；折叠在适配层处理制表符缺陷 |
| HTML 输出/执行 | 不在原生控件范围；不因扩展语法而开启 |

MD4C 的 syntax 字段是保留字段，不是语法插件接口。未来自定义语法另行设计，保持第三方代码
与应用扩展层分离。[Writer demo](../examples/editor/README.md) 已支持段落/标题保持排版直接编辑，
保守源码区间、语义格式模型和块编辑会话均在库层；统一事务仍待实现。
见[块级编辑](block-editing.md)和[后续讨论](editor-follow-up.md)。

## 验证

新增 markdown.extensions：单项语法/关闭状态、嵌套与转义、五种提示块、中文 Wiki 标签、脚注
编号/重复引用/未定义/未引用定义、900 次随机编辑、逐字节流式与全量 AST 对照，以及全局回退。
markdown.extension_views 覆盖样式组合、普通与虚拟视图、Wiki/脚注事件隔离、处理器替换、
主题、复制、提示块自定义和卸载。demo 测试覆盖新样例、事件反馈及流式重放。
