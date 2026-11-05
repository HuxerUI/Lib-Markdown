#include <huxerui/markdown/document.h>
#include "test.h"
#include <random>

using namespace huxerui::markdown;

void Verify(std::string_view source, const MarkdownDocument& document, ParseOptions options) {
  options.source_ranges = false;
  std::size_t previous = 0;
  for (std::size_t i = 0; i < document.Root().children.size(); ++i) {
    const auto range = document.TopLevelSourceRange(i);
    if (!range) continue;
    CHECK(range->start_byte >= previous && range->end_byte <= source.size());
    previous = range->end_byte;
    auto fragment = ParseMarkdown(source.substr(range->start_byte, range->end_byte - range->start_byte), options);
    CHECK(fragment.Root().children.size() == 1);
    CHECK(fragment.Root().children.front() == document.Root().children[i]);
  }
}

int main() { return Run([] {
  const ParseOptions options{.math = true, .preserve_blank_lines = true, .source_ranges = true};
  const std::string source = "# **标题😀**  ##\r\n\r\n中文 &amp; *斜体*\r\n第二行\r\n\r\nSetext\r\n---\r\n\r\n- item\r\n\r\nlast";
  const auto document = ParseMarkdown(source, options);
  Verify(source, document, options);
  std::vector<std::string> editable;
  for (std::size_t i = 0; i < document.Root().children.size(); ++i) {
    if (const auto range = document.TopLevelSourceRange(i))
      editable.push_back(source.substr(range->start_byte, range->end_byte - range->start_byte));
  }
  CHECK(editable == std::vector<std::string>({"# **标题😀**  ##", "中文 &amp; *斜体*\r\n第二行", "Setext\r\n---", "last"}));
  CHECK(!ParseMarkdown("plain").TopLevelSourceRange(0));
  Throws<std::out_of_range>([] { (void)MarkdownDocument{}.TopLevelSourceRange(0); });
  for (const auto sample : {"#\n\nlast", "**\ninside\n**\n\nnext", "[x]: /url\n\n[x]\n", "\xEF\xBB\xBF# heading\n\nbody", "$x$\n\nnext", "\\\nnext", "`a\nb`\n\nnext", "foo\n===\n", "a\0b"})
    Verify(sample, ParseMarkdown(sample, options), options);
  MarkdownParseSession session(options);
  std::mt19937 random(712);
  const std::vector<std::string> tokens{"a", "\n", "\r\n", "**", "# ", "- ", "> ", "`", "&amp;", "$x$", "[r]: /url\n"};
  std::string edited = source;
  for (int n = 0; n < 400; ++n) {
    const auto at = random() % (edited.size() + 1);
    if (at < edited.size() && (static_cast<unsigned char>(edited[at]) & 0xC0) == 0x80) continue;
    edited.insert(at, tokens[random() % tokens.size()]);
    const auto next = session.Update(edited);
    const auto full = ParseMarkdown(edited, options);
    CHECK(next.Root() == full.Root());
    for (std::size_t i = 0; i < next.Root().children.size(); ++i)
      CHECK(next.TopLevelSourceRange(i) == full.TopLevelSourceRange(i));
    Verify(edited, next, options);
  }
}); }
