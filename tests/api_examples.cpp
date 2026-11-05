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

MarkdownDocument IncrementalParsing() {
  MarkdownParseSession session;
  (void)session.Update("# Hello\n\nFirst paragraph.\n\nSecond paragraph.\n");
  auto document = session.ApplyEdit({.start_byte = session.Source().size(),
                                    .inserted_text = "\nAppended paragraph.\n"});
  (void)session.LastParseStats();
  return document;
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

MarkdownDocument Streamed() {
  MarkdownStream stream;
  (void)stream.Append("# Hello\n\n\xF0\x9F");
  (void)stream.Append("\x98\x80\n");
  return stream.Finish();
}

View VirtualArticle(MarkdownDocument document) {
  return VirtualizedMarkdown(document, {}, {.estimated_block_extent = 72.0F})
      .With(Frame{.height = 600.0F});
}

View ExtendedArticle(std::string_view source) {
  auto document = ParseMarkdown(source, {
    .highlight = true, .insertions = true, .underline = true,
    .wiki_links = true, .admonitions = true, .footnotes = true,
  });
  return Markdown(document, {.admonition_renderer = [](const MarkdownNode& node) -> View {
    return Text(PlainText(node));
  }})
      .On<MarkdownEvents::WikiLinkActivated>([](const MarkdownWikiLinkActivation& event) { (void)event.target; })
      .On<MarkdownEvents::FootnoteActivated>([](const MarkdownFootnoteActivation& event) { (void)event.number; });
}
} // namespace markdown_examples
