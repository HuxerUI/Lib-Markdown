#pragma once

#include <array>
#include <functional>
#include <optional>
#include <utility>

#include <huxerui/markdown/document.h>
#include <huxerui/markdown/editing.h>
#include <huxerui/theme.h>
#include <huxerui/view.h>
#include <huxerui/scroll.h>

namespace huxerui::markdown {

/// Complete local appearance override, in DIPs. Omission in MarkdownOptions follows
/// the enclosing HuxerUI theme; an explicit value replaces the complete appearance.
/// Heading sizes and table_cell_width must be finite and positive; spacing, indent,
/// padding, corner_radius and blank_line_height must be finite and nonnegative. Invalid values throw
/// invalid_argument at declaration or composition. This is not an Environment theme.
struct MarkdownStyle {
  TextStyle body{Font::System(16.0F), Color::Rgb(31, 35, 40)};
  TextStyle code{Font::Monospace(14.0F), Color::Rgb(31, 35, 40)};
  Color link = Color::Rgb(9, 105, 218);
  Color muted = Color::Rgb(87, 96, 106);
  Color code_background = Color::Rgb(246, 248, 250);
  Color quote_background = Color::Rgb(246, 248, 250);
  Color table_header_background = Color::Rgb(246, 248, 250);
  Color border = Color::Rgb(208, 215, 222);
  Color highlight_foreground = Color::Rgb(60, 45, 0);
  Color highlight_background = Color::Rgb(255, 235, 150);
  Color spoiler_cover = Color::Rgb(87, 96, 106);
  Color admonition_foreground = Color::Rgb(9, 105, 218);
  Color admonition_background = Color::Rgb(246, 248, 250);
  std::array<float, 6> heading_sizes{32, 26, 22, 20, 18, 16};
  float block_spacing = 12.0F;
  float list_spacing = 4.0F;
  float indent = 24.0F;
  float code_padding = 12.0F;
  float corner_radius = 6.0F;
  float table_cell_width = 180.0F;
  /// Nonnegative finite height per BlankLines line in DIPs; replaces adjacent block gaps.
  float blank_line_height = 20.0F;

  /// Derive a complete appearance from an explicit HuxerUI theme snapshot.
  [[nodiscard]] static MarkdownStyle FromTheme(const ThemeSpec& theme);
};

/// Predicate for an original decoded destination, borrowed only during the call.
/// Invoked during composition, not navigation. Empty uses IsDefaultLinkAllowed.
using MarkdownLinkPolicy = std::function<bool(std::string_view)>;
/// Produces a View for CodeBlock only, on the composing UI thread. The node is
/// borrowed for the call; copy needed fields into delayed callbacks. Invocation
/// count is not stable across recompositions. Exceptions propagate to the caller.
/// Nodes may be render-owned copies; retaining the input document alone does not
/// extend the lifetime of this callback reference.
using MarkdownCodeBlockRenderer = std::function<View(const MarkdownNode&)>;
/// Produces a View for the Image that is the sole child of a paragraph. The same
/// borrowing/thread rules apply as for MarkdownCodeBlockRenderer; inline images
/// use alt text. The application owns URI policy, loading, sizing and placeholders.
using MarkdownStandaloneImageRenderer = std::function<View(const MarkdownNode&)>;
/// Optional replacement for an entire Admonition block, with the same borrowing rules.
using MarkdownAdmonitionRenderer = std::function<View(const MarkdownNode&)>;
/// A math node that is the sole child of a Paragraph. InlineMath or DisplayMath;
/// node.text is literal LaTeX without delimiters. Same borrowing/thread rules as
/// other renderers. Mixed inline math always uses a literal monospace fallback.
using MarkdownStandaloneMathRenderer = std::function<View(const MarkdownNode&)>;

/// Display configuration only. Set ParseOptions with ParseMarkdown before rendering.
struct MarkdownOptions {
  /// Omitted: resolve the ambient theme inside the component's composition scope.
  std::optional<MarkdownStyle> style;
  /// Wrap content in native SelectionArea for selection/copy (default true).
  bool selectable = true;
  /// Omitted: IsDefaultLinkAllowed. Applications may explicitly allow their own schemes.
  MarkdownLinkPolicy link_policy;
  /// Optional rendering of standalone paragraph images. Inline images use alt text.
  /// The callback owns loading, URI policy, sizing, and error placeholders.
  MarkdownStandaloneImageRenderer standalone_image_renderer;
  /// Optional replacement for a fenced/indented code block (e.g. syntax highlighting).
  MarkdownCodeBlockRenderer code_block_renderer;
  MarkdownAdmonitionRenderer admonition_renderer;
  /// Optional application-owned formula backend. Default displays literal LaTeX,
  /// not mathematical typesetting; does not replace math mixed with other text.
  MarkdownStandaloneMathRenderer standalone_math_renderer;
};

/// Owned link information. Destination preserves decoded relative paths, anchors and Unicode.
struct MarkdownLinkActivation {
  /// Decoded original address, without URI normalization or automatic navigation.
  std::string destination;
};

/// Wiki targets are opaque page identifiers; no URI parsing, link_policy or automatic navigation.
struct MarkdownWikiLinkActivation {
  std::string target;
};

/// Requests application-owned navigation to a footnote. Does not emit LinkActivated.
/// Numbers and reference ordinals are 1-based and may change after editing definitions/references.
struct MarkdownFootnoteActivation {
  unsigned number = 0;
  std::string label;
  unsigned reference = 0;
};

/// Events bound on Markdown(...).On<Key>(handler). Run in the owning UI scope;
/// handlers are replaced on recomposition and disconnected after unmounting.
struct MarkdownEvents {
  /// Requests application-owned navigation once per activation. No automatic navigation.
  /// The payload reference is borrowed for the callback; copy it to retain it.
  /// Policy-rejected links do not emit. Missing handlers do nothing; handler exceptions propagate.
  struct LinkActivated : Event<void(const MarkdownLinkActivation&)> {};
  struct WikiLinkActivated : Event<void(const MarkdownWikiLinkActivation&)> {};
  struct FootnoteActivated : Event<void(const MarkdownFootnoteActivation&)> {};
};

/// Native declarative Markdown content. Wrap in ScrollView to scroll vertically.
/// Parses immediately with default ParseOptions, propagating ParseMarkdown exceptions.
/// Returns a deferred UI scope; custom callbacks execute on the composing UI thread.
[[nodiscard]] View Markdown(std::string_view source, MarkdownOptions options = {});
/// Retains a cheap immutable document snapshot without reparsing on recomposition.
/// For nondefault parsing call ParseMarkdown(source, options) first. A default-constructed
/// document displays empty content. Attach MarkdownEvents on the returned View.
[[nodiscard]] View Markdown(MarkdownDocument document, MarkdownOptions options = {});

/// Measured, variable-height top-level block virtualization. Positive finite initial
/// extent estimate; finite nonnegative cache distance, in DIPs. Nested lists/tables
/// and individual large code blocks remain one item. Optional controller addresses
/// top-level block indices. VirtualizedMarkdown owns vertical scrolling.
struct MarkdownVirtualizationOptions {
  float estimated_block_extent = 80.0F;
  float cache_extent = 400.0F;
  std::optional<ScrollController> scroll_controller;
};

/// Requires bounded height; do not wrap in another vertical ScrollView. Only visible
/// and cached top-level blocks create Views. Unchanged prefix/suffix blocks retain
/// identities across snapshots; arbitrary replacements can reset affected selection.
/// With selectable=true, a logical text source supports copying offscreen text without
/// mounting it. List markers are decorative. Custom-rendered code/images contribute
/// source text to full selection, but their unbound custom Text is not an endpoint.
/// The same rule applies to custom admonitions/math; generated titles/definition numbers are decorative.
/// MarkdownEvents bind as on Markdown; callbacks may repeat when items re-enter view.
[[nodiscard]] View VirtualizedMarkdown(MarkdownDocument document, MarkdownOptions options = {},
                                     MarkdownVirtualizationOptions virtualization = {});
[[nodiscard]] View VirtualizedMarkdown(std::string_view source, MarkdownOptions options = {},
                                     MarkdownVirtualizationOptions virtualization = {});

/// Native block editor. Supported paragraphs/headings edit displayed attributed
/// text with hidden Markdown markers. Unsupported inline content falls back to
/// in-place source input; other structures remain rendered/read-only.
struct MarkdownEditorOptions {
  ParseOptions parse_options;
  MarkdownOptions markdown_options;
  MarkdownVirtualizationOptions virtualization;
  /// Show a preview below source fallback/empty-source input. Rich editing renders
  /// directly without a duplicate preview. Fragment previews have local reference scope.
  bool live_preview = true;
  /// Edit supported inline content directly with its typography and hidden markers.
  /// Unsupported blocks retain in-place source editing. Edited blocks are serialized
  /// semantically; source spelling inside that block can change.
  bool rich_text = true;
};

struct MarkdownEditorEvents {
  /// Full source plus UTF-16 selection/composition in whole-source coordinates.
  /// Accept the complete value in the caller's controlled state, as for TextField.
  struct Changed : Event<void(const TextEditingValue&)> {};
  /// An edit was rejected; the last valid value is retained. Borrowed message.
  struct Error : Event<void(const std::string&)> {};
};

/// Owns virtual scrolling and requires bounded height. Click/Tab into a paragraph
/// to edit; blur or Ctrl+Enter finishes it (never while IME composition is active).
/// Uses one attributed native paragraph for painting, hit testing, caret and IME.
/// The native editor owns the active block's text history, with a bounded semantic
/// formatting cache. Cross-block undo and nested/table editing are not yet provided.
/// An empty document presents one native source field so writing can begin.
[[nodiscard]] View MarkdownEditor(TextEditingValue value, MarkdownEditorOptions options = {});
[[nodiscard]] inline View MarkdownEditor(const State<TextEditingValue>& value, MarkdownEditorOptions options = {}) {
  return MarkdownEditor(value.Get(), std::move(options));
}

} // namespace huxerui::markdown
