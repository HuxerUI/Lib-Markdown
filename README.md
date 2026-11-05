# Lib-Markdown

[简体中文](README.zh-CN.md)

The 0.2 API includes source-breaking changes. See the [migration guide](docs/migration-0.2.md).

Native Markdown content for [HuxerUI](https://github.com/HuxerUI/HuxerUI), with a
CommonMark parser, configurable GFM extensions, and ordinary declarative views.

- C++20; header `<huxerui/markdown.h>`, namespace `huxerui::markdown`
- CMake target `HuxerUI::Markdown` (`huxerui_markdown`)
- Headings, paragraphs, nested lists, quotes, rules, fenced and indented code
- Bold, italic, nested emphasis, inline code, strikethrough, and reference links
- GFM tables with alignment and horizontal scrolling, read-only task lists, autolinks
- Native attributed text with Unicode-safe ranges and application-owned link activation
- Selection/copy through `SelectionArea`, ambient light/dark themes, custom code/image renderers
- Immutable documents that can be parsed on a worker and reused across recompositions
- Stateful incremental parsing with UTF-8 edits, safe prefix reuse and parse statistics
- Vendored MIT-licensed MD4C; no runtime download, WebView, JavaScript, or HTML rendering

The component uses HuxerUI's platform-neutral views and supports its Windows,
macOS, Linux, Android, iOS, and Web backends. The initial native build and runtime
tests were verified on Windows; CI also checks the parser on Linux and macOS.

## Quick start

```cmake
# After huxerui_add_app(your_app ...):
huxerui_use_library(your_app
    TARGET HuxerUI::Markdown
    PATH "/path/to/Lib-Markdown")
```

```cpp
#include <huxerui/huxerui.h>
#include <huxerui/markdown.h>

using namespace huxerui;
using namespace huxerui::markdown;

View Article() {
  return ScrollView(Markdown("# Hello\n\n**Native** Markdown.")
    .On<MarkdownEvents::LinkActivated>([](const MarkdownLinkActivation& event) {
      // Navigate using your application's routing policy.
      // event.destination preserves the original decoded address.
    }));
}
```

`Markdown` returns content without vertical scrolling. Wrap it in a bounded
`ScrollView` as appropriate for your layout. Tables scroll horizontally on their own.

## Reuse a prepared document

```cpp
// May run on a worker; source storage is copied into an owned syntax tree.
auto document = ParseMarkdown(source);

// UI composition; retaining document avoids parsing on each recomposition.
return Markdown(document);
```

`MarkdownDocument::Root()` exposes a read-only syntax tree; `PlainText()` extracts
visible text. `MarkdownDocument` is a value handle: copies share immutable storage,
and equality compares snapshot identity. Default construction creates a valid empty
document; moving leaves the source valid and sharing the same snapshot. Access nodes
with `document.Root()`, retaining an owner while using borrowed node references.
The parser-only public API is `<huxerui/markdown/document.h>` and
does not depend on HuxerUI. `ParseOptions` can disable individual extensions,
make soft line breaks hard, or lower the input/depth limits.

The source overload of `Markdown` uses default parsing. For custom parsing, prepare
the document explicitly; display options never contain ignored parse settings:

```cpp
auto document = ParseMarkdown(source, {
  .soft_break_mode = SoftBreakMode::LineBreak,
});
return Markdown(document);
```

## Incremental updates

Keep one parser session for successive edits; publish its immutable snapshots to the UI:

```cpp
MarkdownParseSession parser;
auto document = parser.Update(source);
document = parser.ApplyEdit({.start_byte = parser.Source().size(),
                             .inserted_text = "\nAppended paragraph.\n"});
auto stats = parser.LastParseStats();
// Or pass the entire revised source to parser.Update(next_source).
```

Edit coordinates are UTF-8 bytes at scalar boundaries, not HuxerUI UTF-16 offsets.
The session reparses a tail from a parser-confirmed unchanged top-level boundary,
reusing earlier blocks. Potential reference definitions or no safe boundary cause
a full parse. Equal input preserves snapshot identity. Failures preserve the last
successful source, document and statistics. Sessions are synchronous and require
serialized access; snapshots can be retained independently.

This reduces parser work for suitable edits; source scans and retained AST copies
remain linear. It does not provide incremental rendering or virtualization. See
[the parsing contract](docs/incremental-parsing.md) for boundaries and diagnostics.

## Themes and custom rendering

The default appearance follows the enclosing HuxerUI theme, including font family,
body size, foregrounds, and surfaces. Use `MarkdownOptions::style` for a complete
local appearance override; it is not an inheritable Markdown theme environment:

```cpp
auto style = MarkdownStyle::FromTheme(UseTheme());
style.body.font = style.body.font.WithSize(18.0F);
style.table_cell_width = 220.0F;

return Markdown(document, {
  .style = style,
  .selectable = true,
  .code_block_renderer = [](const MarkdownNode& code) -> View {
    // code.text includes newlines; code.language and code.fence_info preserve the fence info.
    return Text(code.text).Style({Font::Monospace(14.0F), Color::Rgb(40, 50, 60)});
  },
});
```

`standalone_image_renderer` receives a standalone paragraph's image node (`destination`,
`title`, and alt text via `PlainText(node)`). Supply your loader, URI policy,
dimensions, and placeholder there. Images otherwise render alt text; embedded
inline images always render alt text. No image is fetched automatically.
Both renderer callbacks execute during UI composition and borrow their node for
that call. Copy needed fields or retain the document for delayed callbacks. They
may run again on recomposition and do not guarantee a particular invocation count.

## Links and input behavior

`MarkdownEvents::LinkActivated` receives `MarkdownLinkActivation::destination`, the
original decoded address, including Unicode paths, `../relative.md` and `#anchors`.
Handlers run in the owning UI scope, update on recomposition, and disconnect on
unmounting. Copy the payload to retain it after the callback. HuxerUI never opens
the address automatically. By default, only HTTP(S),
mailto, and relative targets are interactive. Explicit custom schemes require
`link_policy`. Rejected destinations stay readable as plain text. The predicate
`IsDefaultLinkAllowed()` describes the default allow policy, not a URI-validation
or navigation-safety guarantee. Resolve relative
paths against an application-controlled base before opening them.

Raw HTML is displayed literally. HTML entities are decoded in ordinary text and
attributes, but remain literal in code. Invalid UTF-8 is rejected; embedded NUL
and invalid numeric entity scalars are replaced with U+FFFD. Defaults limit input
to 8 MiB and nesting to 128; the nesting limit can be configured up to 512.

## Interactive demo

The [standalone Markdown Playground](examples/preview/README.md) provides editable
source beside a native preview, five switchable samples, light/dark themes, and
custom code/image renderer examples. Live preview is enabled with a cancellable
120 ms debounce; pause it to render manually. Parse statistics show prefix reuse.
Narrow windows
stack the panels; the source panel can also be hidden.

```sh
cmake -S examples/preview -B build-preview -DHUXERUI_HOME=/path/to/HuxerUI
cmake --build build-preview --config Release --parallel
```

With Visual Studio, launch `build-preview/Release/markdown_preview.exe`; keep its
`markdown_preview.resources` directory alongside it. CI publishes a Windows demo
artifact after its standalone build and interaction tests pass.

## Build and test

Requires CMake 3.20+, a C99/C++20 toolchain, and a recent HuxerUI source tree or
SDK with `AttributedText`, `TextEvents::LinkActivated`, and `SelectionArea`.
The initial implementation was verified with HuxerUI commit
`cadb1696cfa7b87d9d2d316b3ecba07b85a46962`.

```sh
cmake -S . -B build -DHUXERUI_HOME=/path/to/HuxerUI \
  -DHUXERUI_MARKDOWN_BUILD_TESTS=ON \
  -DHUXERUI_BUILD_TESTING_LIBRARY=ON \
  -DHUXERUI_MARKDOWN_BUILD_EXAMPLES=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

`HUXERUI_BUILD_TESTING_LIBRARY=ON` enables headless component interaction tests
when building HuxerUI from source. Without it, view tests still cover attributed
text, theme derivation, option validation, and link policy. Run `markdown_preview`
from the build output to see mixed Chinese/English content and theme switching.
With the testing library, the event suite also verifies instance isolation,
handler replacement, original destinations, and unmount disconnection. Public API
examples are compiled from [tests/api_examples.cpp](tests/api_examples.cpp).

For a standalone parser build without HuxerUI:

```sh
cmake -S . -B build-parser -DHUXERUI_MARKDOWN_BUILD_VIEW=OFF \
  -DHUXERUI_MARKDOWN_BUILD_TESTS=ON
cmake --build build-parser --config Release --parallel
ctest --test-dir build-parser -C Release --output-on-failure
```

HuxerUI library-graph discovery and project-plan discovery are supported without
enabling compilers or building MD4C. The library has no generated resource package.

## Scope

Task checkboxes are presentation only. Code blocks use wrapped monospace text by
default; provide a renderer for highlighting or different scrolling. Table columns
have a configurable fixed width, with per-cell wrapping and GFM alignment.
Footnotes, math, diagrams, HTML execution, a reusable editor component and document
virtualization are outside this version. Stream chunks can be applied through the
session, but there is no separate streaming grammar or finalization API.

## License

MIT. See [LICENSE](LICENSE) and the vendored [MD4C license](third_party/md4c/LICENSE.md).
