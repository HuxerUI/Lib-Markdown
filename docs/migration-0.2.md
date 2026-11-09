# Migrating from 0.1 to 0.2 / 迁移到 0.2

0.2 normalizes the public API. This is a **source- and binary-incompatible** update:
rebuild the library and its consumers together. There are no legacy option fields
or compatibility overloads. Namespace, headers and the `HuxerUI::Markdown` CMake
target are unchanged. Default syntax is unchanged; additional syntax is opt-in.

0.2 调整了公开 API，需要同步修改调用代码并重新编译库与应用；不保留两套配置字段。
命名空间、头文件与 CMake 目标保持不变，默认语法保持不变；新增语法通过选项显式启用。

## Names / 名称对照

| 0.1 | 0.2 |
| --- | --- |
| `MarkdownTheme` | `MarkdownStyle` |
| `MarkdownOptions::theme` | `MarkdownOptions::style` |
| `MarkdownOptions::parse` | Pass `ParseOptions` to `ParseMarkdown` explicitly |
| `MarkdownOptions::on_link` | `.On<MarkdownEvents::LinkActivated>(handler)` |
| `image_renderer` | `standalone_image_renderer` |
| `code_renderer` | `code_block_renderer` |
| `ParseOptions::hard_soft_breaks = false` | `soft_break_mode = SoftBreakMode::Space` (default) |
| `ParseOptions::hard_soft_breaks = true` | `soft_break_mode = SoftBreakMode::LineBreak` |
| `IsSafeMarkdownLink` | `IsDefaultLinkAllowed` |
| `MarkdownNode::info` | `MarkdownNode::fence_info` |
| `NodeKind::Quote` | `NodeKind::BlockQuote` |
| `NodeKind::Rule` | `NodeKind::ThematicBreak` |

## Document values / 文档值句柄

`ParseMarkdown` returns `MarkdownDocument`, not a shared pointer. Copies share
immutable storage; they do not copy the AST. Replace arrow access with dot access:

```cpp
auto document = ParseMarkdown(source);
const auto& root = document.Root();
std::string text = document.PlainText();
return Markdown(document);
```

Declare retained state as `UseState(MarkdownDocument{})` instead of storing
`std::shared_ptr<const MarkdownDocument>`. Default construction creates a valid
empty document. Copies compare equal by snapshot identity; separately parsing
the same text produces distinct snapshots. Compare `Root()` values explicitly
only when a deep AST comparison is actually needed.

Moves retain a valid source sharing the same snapshot. Keep a document owner alive
while borrowing references from `Root()`; replacing the last owner invalidates
those references. The input string need only survive the parse call.

用 `document.Root()` / `document.PlainText()` 替换箭头访问，State 直接保存文档值。
相等比较基于快照身份，不再需要或支持通过公开指针地址识别文档。
例如 demo 在切换或重置样例时递增自己的版本号，并用该版本号重置滚动位置；编辑保留滚动。

## Parsing and display options / 解析与显示配置

Before:

```cpp
MarkdownOptions options;
options.parse.hard_soft_breaks = true;
return Markdown(source, options);
```

After:

```cpp
auto document = ParseMarkdown(source, {
  .soft_break_mode = SoftBreakMode::LineBreak,
});
return Markdown(document, {.selectable = true});
```

`Markdown(source)` remains the default-parse convenience overload. Custom parsing
always happens explicitly, so rendering an already parsed document cannot silently
ignore parsing options. SoftBreakMode does not change explicit hard breaks or code
normalization. Invalid enum values throw `std::invalid_argument`.

## Link events / 链接事件

Before:

```cpp
return Markdown(source, {
  .on_link = [](const std::string& destination) { Navigate(destination); },
});
```

After:

```cpp
return Markdown(source)
    .On<MarkdownEvents::LinkActivated>([](const MarkdownLinkActivation& event) {
      Navigate(event.destination);
    });
```

`Navigate` represents your application routing function. `destination` preserves
the original decoded relative/Unicode address or anchor. Native synthetic URI
identities are never exposed. One activation emits once; denied links do not emit.
No handler means no navigation. Handlers run in the component's UI scope, use the
latest binding after recomposition, and stop receiving events after unmounting.
The payload reference is valid for the callback; copy it for later use. Handler
exceptions propagate according to HuxerUI's event rules.

链接策略仍由 `link_policy` 控制；`IsDefaultLinkAllowed` 只是默认允许策略，
不代表 URI 完整校验或导航安全保证。移除旧 on_link 后不会出现两套回调重复通知。

## Style and custom renderers / 样式与自定义渲染

Inside UI composition:

```cpp
auto style = MarkdownStyle::FromTheme(UseTheme());
style.table_cell_width = 220.0F;

return Markdown(document, {
  .style = style,
  .code_block_renderer = [](const MarkdownNode& code) -> View {
    // code.language and code.fence_info describe the code block.
    return Text(code.text);
  },
});
```

Omitting style continues to follow the surrounding HuxerUI theme. An explicit
MarkdownStyle replaces the complete local appearance; it is not an environment
theme or a collection of partial overrides. The renamed callbacks keep their old
scope: code blocks only, and standalone paragraph images only. Inline code and
inline images are not newly routed to these callbacks.

`MarkdownLinkPolicy`, `MarkdownCodeBlockRenderer` and
`MarkdownStandaloneImageRenderer` name the callback types. Renderer nodes are
borrowed for one composition-time call, and calls may repeat. Copy data when
building delayed callbacks; nodes can be render-owned copies, so retaining only
the input snapshot does not retain a callback reference. Loading and other side effects
remain application-owned.

## Upgrade checklist / 升级检查

- Replace document pointer declarations/access and migrate document state.
- Move parsing configuration into ParseMarkdown; replace the soft-break boolean.
- Replace event callbacks with typed event bindings.
- Rename local styles, renderer fields and AST terminology.
- Rebuild the parser/library, application, demo and any precompiled dependents.
- Check actual links, selection/copy, theme changes and custom renderers.

The follow-up adds live preview and the optional `MarkdownParseSession` API;
existing one-shot parsing needs no additional migration. See the
[incremental parsing contract](incremental-parsing.md). Additional opt-in APIs now
include `MarkdownStream`, `MarkdownParseSession::Append()` and `VirtualizedMarkdown`;
see [streaming and virtualization](streaming-and-virtualization.md).
Global Markdown theme inheritance is not included.
Six optional MD4C extensions add AST kinds/fields, styles, an admonition renderer
and typed Wiki/footnote events; see [the extension contract](md4c-extensions.md).
NodeKind numeric values are not a serialization ABI. Rebuild all consumers and
update exhaustive node visitors; no compatibility aliases are provided.
Additional opt-in math/blank-line support is documented in
[math and blank lines](math.md). The 0.2 source-format commands and Writer editing
demo were removed in 0.3; see the [0.3 migration guide](migration-0.3.md).

`ParseOptions::autolinks` is now `MarkdownAutolinkOptions{urls, emails, www}`.
Replace `.autolinks = false` with `.autolinks = {false, false, false}`; explicit
links and angle autolinks remain independent. Syntax presets are available as
`ParseOptions::CommonMark()` and `ParseOptions::GitHub()`; HTML remains literal.
Spoilers and additional parser switches are opt-in; rebuild visitors for the new Spoiler kind.
Compiled target-API examples are available in
[tests/api_examples.cpp](../tests/api_examples.cpp).
