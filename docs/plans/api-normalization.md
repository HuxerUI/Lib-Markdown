# Lib-Markdown API 规范化计划

状态：已实施并完成本地验收；文中的目标接口已成为 0.2 源码 API。

日期：2026-11-05。当前实现基线：`db12c3c30938d539d0369458d5412813c761cc18`。
库及独立 demo 的源码版本已更新为 `0.2.0`；未创建发布标签或 GitHub Release。

## 1. 目标与范围

本轮解决公开 API 的命名、职责划分和 HuxerUI 接入一致性：

- 使用官方 Lib 已采用的类型化事件，而非组件配置中的事件回调。
- 明确区分解析选项、组件配置和组件外观。
- 将文档共享存储封装在轻量值类型内。
- 修正含义模糊、覆盖范围不明确或承诺过强的名称。
- 同步迁移实现、测试、demo 和中英文文档。

本轮不增加实时预览、文档虚拟化、增量解析、流式追加、后台解析任务、全局 Markdown
主题环境或新的 Markdown 语法。上述能力需要单独设计及验收，不以新增空接口代替实现。

## 2. 参考依据

以下为本次审查采用的官方接口快照，不以 Lib-CodeEditor 或本库自身作为规范依据。
这些是从实际公开接口归纳出的约定，不视为组织已发布的统一强制规范。

| 参考 | 用途 |
| --- | --- |
| [Lib-Charts](https://github.com/HuxerUI/Lib-Charts/blob/e281c5e38576a19bc50501e97543dafc9922ac8c/include/huxerui/charts.h) | 不可变共享数据值、Style/Theme 区别、类型化事件、回调类型别名 |
| [Lib-Camera](https://github.com/HuxerUI/Lib-Camera/blob/cada0539098791019bc5330ffd87009acc29d68a/include/huxerui/camera.h) | 按职责拆分 Options、组件返回 View、命名空间内的前缀使用 |
| [Lib-Lottie](https://github.com/HuxerUI/Lib-Lottie/blob/7dad80083cdd48ed2c88ac228969fc8027d55e07/include/huxerui/lottie.h) | View 工厂函数、Events 分组及事件生命周期契约 |
| [Lib-Live2D](https://github.com/HuxerUI/Lib-Live2D/blob/936694bcebddd8238e08ade1a8a8ea267dcdbb98/include/huxerui/live2d.h) | 封装共享存储的资源句柄、Options 与事件的分工 |
| [HuxerUI 事件接口](https://github.com/HuxerUI/HuxerUI/blob/cadb1696cfa7b87d9d2d316b3ecba07b85a46962/include/huxerui/event.h) | UseEvents / EventEmitter，显式向组件所属作用域发出事件 |
| [CommonMark cmark](https://github.com/commonmark/cmark/blob/master/src/cmark.h) | BlockQuote、ThematicBreak 和 fence info 等语法术语；不照搬其 C 命名风格 |

## 3. 命名约定

1. 类型、枚举项、函数和方法使用 PascalCase；公开字段使用 snake_case。
2. 保留 `huxerui::markdown`、`<huxerui/markdown.h>`、`<huxerui/markdown/document.h>`
   和 `HuxerUI::Markdown`。
3. 保留 `Markdown(...)`、`ParseMarkdown(...)`、`MarkdownDocument`、`MarkdownNode`、
   `MarkdownOptions`、`ParseOptions`、`NodeKind`、`CellAlignment`、`Root()` 和 `PlainText()`。
4. 不要求命名空间内的每个类型都重复 Markdown 前缀，也不批量移除现有前缀。
5. `Options` 表示行为配置，`Style` 表示组件外观，`Events` 表示交互通知。
   本轮不把局部样式称为可继承的 Theme。
6. 回调名称说明实际覆盖范围。未来只有返回异步任务的 API 使用 `Async` 后缀；
   只有实际读取并保留组合状态的 hook 使用 `Use` 前缀。

## 4. 确定的命名迁移

| 当前名称 | 目标名称或用法 | 行为约束 |
| --- | --- | --- |
| `MarkdownTheme` | `MarkdownStyle` | 本轮定位为单组件的完整外观值 |
| `MarkdownTheme::FromTheme()` | `MarkdownStyle::FromTheme()` | 从 HuxerUI ThemeSpec 解析默认外观 |
| `MarkdownOptions::theme` | `MarkdownOptions::style` | 未提供时跟随当前 HuxerUI 主题 |
| `MarkdownOptions::parse` | 删除，移至显式 `ParseMarkdown(source, ParseOptions)` 调用 | 已解析文档的显示接口不再接收无效的解析配置 |
| `MarkdownOptions::on_link` | `.On<MarkdownEvents::LinkActivated>(handler)` | 明确实现组件级事件转发 |
| `code_renderer` | `code_block_renderer` | 仅覆盖围栏代码块和缩进代码块，不覆盖行内代码 |
| `image_renderer` | `standalone_image_renderer` | 仅覆盖独占段落图片；行内图片保持替代文字行为 |
| `ParseOptions::hard_soft_breaks` | `ParseOptions::soft_break_mode` | 使用 `SoftBreakMode::Space` / `LineBreak`，默认 Space |
| `IsSafeMarkdownLink()` | `IsDefaultLinkAllowed()` | 表达默认允许策略，不宣称完整的安全保证 |
| `MarkdownNode::info` | `MarkdownNode::fence_info` | 保留代码围栏的完整 info string；非适用节点为空 |
| `NodeKind::Quote` | `NodeKind::BlockQuote` | 行为不变 |
| `NodeKind::Rule` | `NodeKind::ThematicBreak` | 行为不变 |

`MarkdownNode::tight`、`NodeKind::Code`、`NodeKind::Strong` 等含义明确的语法名称保留；
不把所有布尔字段机械改为 `is_*`。本轮也不调整字体、颜色、缩进和表格列宽的实际默认值。

## 5. 目标 API 契约

### 5.1 解析与文档值

```cpp
enum class SoftBreakMode { Space, LineBreak };

// 其他现有解析字段保留。
struct ParseOptions {
  bool tables = true;
  bool strikethrough = true;
  bool task_lists = true;
  bool autolinks = true;
  SoftBreakMode soft_break_mode = SoftBreakMode::Space;
  std::size_t max_input_bytes = 8 * 1024 * 1024;
  std::size_t max_nesting = 128;
};

MarkdownDocument ParseMarkdown(std::string_view source, ParseOptions options = {});
```

- `MarkdownDocument` 内部持有共享的只读存储，公开调用不再使用
  `std::shared_ptr<const MarkdownDocument>`。
- 默认构造表示有效空文档，具有 Document 根节点；不另设需要调用方判断的空指针状态。
- 复制共享同一快照，不复制语法树。`Root()` 返回只读借用，`PlainText()` 返回拥有数据的字符串。
  保留对应快照的所有者，才能继续使用借用的节点引用。
- 相等比较按快照身份定义，便于低成本接入 State；不隐含深度 AST 比较。
  分别解析相同文本产生的不同快照可以不相等。
- 输入字符串仍无需持续存活。不可变快照允许工作线程准备后交付 UI；本轮不增加线程调度 API。
- 保留当前 UTF-8 校验、NUL/实体处理、输入及深度限制和异常分类。

### 5.2 显示选项与局部样式

```cpp
using MarkdownLinkPolicy = std::function<bool(std::string_view)>;
using MarkdownCodeBlockRenderer = std::function<View(const MarkdownNode&)>;
using MarkdownStandaloneImageRenderer = std::function<View(const MarkdownNode&)>;

struct MarkdownOptions {
  std::optional<MarkdownStyle> style;
  bool selectable = true;
  MarkdownLinkPolicy link_policy;
  MarkdownStandaloneImageRenderer standalone_image_renderer;
  MarkdownCodeBlockRenderer code_block_renderer;
};

View Markdown(std::string_view source, MarkdownOptions options = {});
View Markdown(MarkdownDocument document, MarkdownOptions options = {});
```

- 源码重载使用默认 ParseOptions；非默认解析配置先调用 ParseMarkdown，再传文档值。
  不增加一组难区分的三参数重载。
- 省略 style 时，在组件组合作用域内根据当前 ThemeSpec 派生外观，继续跟随主题变化。
- 显式 style 是完整外观值。本轮不引入逐字段可选覆盖或 `ProvideEnvironment(MarkdownTheme, ...)`。
- 自定义 renderer 返回普通 View；接收节点的引用仅在回调期间借用。
  在返回 View 的异步/延迟回调中使用节点信息时，应复制需要的数据或保留文档快照。
- renderer 的调用次数不稳定，不应承担一次性外部操作；图片加载及 URI 策略仍由应用负责。
- `link_policy` 保留当前空值默认策略。`IsDefaultLinkAllowed()` 的判断与旧函数保持一致，
  不新增 URL 解析、相对路径解析、浏览器打开或网络请求行为。

### 5.3 组件级链接事件

```cpp
struct MarkdownLinkActivation {
  std::string destination;
};

struct MarkdownEvents {
  struct LinkActivated : Event<void(const MarkdownLinkActivation&)> {};
};
```

- destination 为原始解码后的 Markdown 地址，保留相对路径、锚点和 Unicode；
  不泄漏当前用于连接原生文本接口的合成 URI。
- 本轮不暴露段落局部偏移、文档偏移或节点 ID，避免在坐标和稳定身份尚未定义时形成兼容承诺。
- 在组件自身的 Scope 获取 EventEmitter，由内部 Text 的 LinkActivated 回调显式发出
  MarkdownEvents::LinkActivated；不假设子节点事件会自动冒泡到外层 View。
- 一个有效激活只通知一次；被策略拒绝的链接不触发事件。
- 事件在所属 UI 作用域执行。重组后使用最新 handler，组件卸载后旧 emitter 不继续通知。
- 用户 handler 抛出的异常保持框架传播规则；无 handler 时不自动导航。

### 5.4 目标调用示例

以下展示已实施的 0.2 用法；历史 0.1 基线版本不支持这些接口：

```cpp
auto document = ParseMarkdown(source, {
  .soft_break_mode = SoftBreakMode::LineBreak,
});

return Markdown(document, {.selectable = true})
    .On<MarkdownEvents::LinkActivated>([](const MarkdownLinkActivation& event) {
      // 应用负责导航；event.destination 可能是相对地址。
    });
```

## 6. 实施顺序与验收

每阶段在同一改造分支中完成源码、测试和使用点迁移，保持可编译；最终统一交付新 API。

### 阶段一：解析层语义命名

- [x] 增加 SoftBreakMode，替换布尔换行选项，拒绝无效枚举值。
- [x] 重命名 BlockQuote、ThematicBreak、fence_info 和默认链接策略函数。
- [x] 更新 src/document.cpp、src/markdown.cpp 和对应解析/富文本测试。

验收：默认软换行仍为空格，LineBreak 保留显示换行；显式硬换行和代码内换行不受影响。
语法树除约定名称外保持原有结构和内容；默认链接允许/拒绝用例保持一致。
独立 parser 构建和测试通过，仍不依赖 HuxerUI。

### 阶段二：文档句柄与选项边界

- [x] 将文档树移入私有共享存储，使 ParseMarkdown 返回 MarkdownDocument 值。
- [x] 明确定义默认空文档、复制/移动、借用生命周期和身份相等语义。
- [x] 拆除 MarkdownOptions::parse，迁移自定义解析配置到 ParseMarkdown 调用点。
- [x] 完成 MarkdownStyle / style 及 renderer 名称、回调类型别名迁移。
- [x] 更新 demo 的 State、快照身份 key 及相关测试，移除基于公开文档指针的用法。

验收：文档副本在原对象销毁后仍可使用，空文档正常显示，相同快照复制成本不随 AST 大小增长。
重组和主题切换不会重新解析准备好的文档；非默认解析选项只在解析阶段生效。
默认/自定义渲染、明暗主题、图片替代文字和选择复制行为保持一致。

### 阶段三：类型化事件

- [x] 定义 MarkdownLinkActivation 和 MarkdownEvents::LinkActivated。
- [x] 在组件作用域建立 EventEmitter，将内部文本激活映射为组件事件。
- [x] 删除 on_link 字段，迁移 demo、测试及全部示例的事件绑定。

验收至少覆盖：普通/相对/Unicode/锚点链接、相邻链接、嵌套强调链接、被拒绝地址、
自定义策略、无 handler、同页两个 Markdown 实例、重组后替换 handler、卸载后停止通知。
每次激活只回调一次，回传原始地址，且没有自动导航副作用。

### 阶段四：文档、兼容说明与完整回归

- [x] 更新两份公开头文件的默认值、单位、异常、线程和借用生命周期说明。
- [x] 更新 README.md、README.zh-CN.md、examples/preview/README.md 和源码示例。
- [x] 增加 v0.1 → v0.2 的逐项迁移说明及可编译的新 API 示例。
- [x] 在计划之外的正式接口及使用点中清理旧名称；迁移文档可以保留旧 API 作为对照。
- [x] 执行独立 parser、完整组件、根项目 demo、独立 demo 的构建与适用测试。
- [x] 验证 library/app project-plan、library-graph 发现和 CI 的 demo 产物路径。

验收：公开示例实际编译，现有行为测试保留并通过，新增契约测试通过。
CI 覆盖 Windows/macOS/Linux parser，以及 Windows 组件和独立 demo。
截图/人工 UI 核验仅验证显示与交互，不替代事件和生命周期测试。

## 7. 兼容策略

本轮按一次有迁移说明的 0.x 破坏性变更规划，不声称旧源代码可直接编译：

- ParseMarkdown 的返回类型变化不能通过仅按返回类型重载来兼容。
- 聚合配置字段重命名会影响 designated initializer；不同时保留两套字段制造优先级歧义。
- 不同时保留 on_link 和类型化事件，避免一次激活产生两次业务通知。
- 不额外增加 LegacyOptions 或隐藏的旧配置适配路径；调用方按迁移表统一更新并重新编译。
- 合并前清点已知 demo、文档和实际下游使用点，迁移说明明确源兼容与二进制兼容影响。
  源码版本已更新为 0.2.0；正式标签和 Release 发布不属于本次 API 实现。

典型迁移：`document->Root()` → `document.Root()`，`document->PlainText()` →
`document.PlainText()`；State 直接保存 MarkdownDocument。demo 若需要在新快照时重置滚动，
使用 demo 自己的文档版本 key，不通过公开原始指针地址实现。

## 8. 后续能力的边界

后续进展（2026-11-05）：实时预览与独立解析会话已按追加需求实现，具体边界见
[增量解析契约](../incremental-parsing.md)。以下清单保留规范化阶段的范围记录，虚拟化仍未实现。

完成本轮后，分别设计并交付以下工作，不在规范化补丁中预留未实现的公开方法：

1. demo 自动预览：输入更新策略、可取消的防抖、错误状态展示。
2. 块级虚拟化：稳定块身份、可见区挂载、滚动锚定及跨虚拟区域选择/复制。
3. 增量文档更新：明确编辑范围的坐标单位、版本关系和影响区，再设计缓存与解析复用。
4. 流式输入：明确暂未闭合结构、引用定义反向影响和结束输入语义。

MarkdownDocument 在本轮始终是不可变快照。未来若增加可变文档会话或控制器，应另行定义
与快照的关系，不能直接向该快照塞入含糊的 Update/Append 方法。

## 9. 实施与本地验收记录

2026-11-05 完成全部四阶段，实现和使用点已同步迁移。

| 检查 | 结果 |
| --- | --- |
| 独立 parser 构建及 CTest | 通过，仍不依赖 HuxerUI |
| 完整组件、根项目 demo、公开 API 示例编译 | 通过 |
| parser / events / view / preview 四组测试 | 全部通过，共 190 项检查 |
| 独立 demo 构建及交互测试 | 通过 |
| library/app project-plan 配置及 JSON | 通过，kind 和 target 保持正确 |
| library-graph 配置及依赖 JSON | 通过，公开目标仍为 HuxerUI::Markdown |
| 旧名称清理 | 正式接口、实现、测试及主文档已迁移；旧名仅保留在迁移/历史对照中 |

事件回归覆盖原始实体解码地址、相对和 Unicode 路径、锚点、相邻链接、嵌套强调、
默认拒绝和自定义允许策略、无处理器、两实例隔离、处理器替换和卸载断连。
同时验证跨段落选择复制，以及主题变化时继续复用同一文档快照。

已清点并迁移本仓库的公开头、实现、测试、demo 和中英文文档。本地同级 Lib 扫描及
官方组织已索引代码搜索没有发现额外已知调用点；该结果不保证覆盖未索引或私有外部源码。
CI 保持三平台 parser、Windows 组件及独立 demo 的覆盖，自动包含新增事件和示例编译检查。
迁移说明位于 [migration-0.2.md](../migration-0.2.md)。
