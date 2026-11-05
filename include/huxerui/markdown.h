#pragma once

#include <array>
#include <functional>
#include <optional>

#include <huxerui/markdown/document.h>
#include <huxerui/theme.h>
#include <huxerui/view.h>

namespace huxerui::markdown {

struct MarkdownTheme {
  TextStyle body{Font::System(16.0F), Color::Rgb(31, 35, 40)};
  TextStyle code{Font::Monospace(14.0F), Color::Rgb(31, 35, 40)};
  Color link = Color::Rgb(9, 105, 218);
  Color muted = Color::Rgb(87, 96, 106);
  Color code_background = Color::Rgb(246, 248, 250);
  Color quote_background = Color::Rgb(246, 248, 250);
  Color table_header_background = Color::Rgb(246, 248, 250);
  Color border = Color::Rgb(208, 215, 222);
  std::array<float, 6> heading_sizes{32, 26, 22, 20, 18, 16};
  float block_spacing = 12.0F;
  float list_spacing = 4.0F;
  float indent = 24.0F;
  float code_padding = 12.0F;
  float corner_radius = 6.0F;
  float table_cell_width = 180.0F;

  /// Derive typography and colors from an ambient HuxerUI theme.
  [[nodiscard]] static MarkdownTheme FromTheme(const ThemeSpec& theme);
};

struct MarkdownOptions {
  ParseOptions parse;
  /// Omitted: resolve the ambient theme inside the component's composition scope.
  std::optional<MarkdownTheme> theme;
  bool selectable = true;
  /// Called with the original decoded Markdown destination, including relative URLs.
  /// The component never opens a browser or fetches a destination automatically.
  std::function<void(const std::string&)> on_link;
  /// Omitted: IsSafeMarkdownLink. Applications may explicitly allow their own schemes.
  std::function<bool(std::string_view)> link_policy;
  /// Optional rendering of standalone paragraph images. Inline images use alt text.
  /// The callback owns loading, URI policy, sizing, and error placeholders.
  std::function<View(const MarkdownNode&)> image_renderer;
  /// Optional replacement for a fenced/indented code block (e.g. syntax highlighting).
  std::function<View(const MarkdownNode&)> code_renderer;
};

/// Native declarative Markdown content. Wrap in ScrollView to scroll vertically.
/// Source overload parses immediately; use a prepared document to avoid parsing on recomposition.
[[nodiscard]] View Markdown(std::string_view source, MarkdownOptions options = {});
[[nodiscard]] View Markdown(std::shared_ptr<const MarkdownDocument> document, MarkdownOptions options = {});

} // namespace huxerui::markdown
