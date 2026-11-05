// Compile the public examples as part of the full library build.
#include <huxerui/huxerui.h>
#include <huxerui/markdown.h>

namespace markdown_examples {
using namespace huxerui;
using namespace huxerui::markdown;

View Article() {
  return ScrollView(Markdown("# Hello\n\n**Native** Markdown.")
      .On<MarkdownEvents::LinkActivated>([](const MarkdownLinkActivation& event) {
        (void)event.destination;
      }));
}

View CustomParsing(std::string_view source) {
  auto document = ParseMarkdown(source, {.soft_break_mode = SoftBreakMode::LineBreak});
  return Markdown(document);
}

View Styled(MarkdownDocument document) {
  return Scope([document] {
    auto style = MarkdownStyle::FromTheme(UseTheme());
    style.body.font = style.body.font.WithSize(18.0F);
    style.table_cell_width = 220.0F;
    return Markdown(document, {
      .style = style,
      .selectable = true,
      .code_block_renderer = [](const MarkdownNode& code) -> View {
        return Text(code.text).Style({Font::Monospace(14.0F), Color::Rgb(40, 50, 60)});
      },
    });
  });
}

View CustomImage(MarkdownDocument document) {
  return Markdown(document, {.standalone_image_renderer = [](const MarkdownNode& image) -> View {
    return Text(PlainText(image));
  }});
}
} // namespace markdown_examples
