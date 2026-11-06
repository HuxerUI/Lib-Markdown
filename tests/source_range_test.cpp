#include <huxerui/markdown/document.h>
#include "test.h"
#include <algorithm>
#include <array>
#include <random>

using namespace huxerui::markdown;

void RemoveBlanks(MarkdownNode& node) {
  std::erase_if(node.children, [](const auto& child) { return child.kind == NodeKind::BlankLines; });
  for (auto& child : node.children) RemoveBlanks(child);
}

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
    auto actual = fragment.Root().children.front();
    auto expected = document.Root().children[i];
    RemoveBlanks(actual);
    RemoveBlanks(expected);
    CHECK(actual == expected);
  }
}

void VerifyNestedRanges(const MarkdownDocument& next, const MarkdownDocument& full,
                        const MarkdownNode& node, std::vector<std::size_t>& path) {
  CHECK(next.SourceRange(path) == full.SourceRange(path));
  for (std::size_t i = 0; i < node.children.size(); ++i) {
    path.push_back(i);
    VerifyNestedRanges(next, full, node.children[i], path);
    path.pop_back();
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
  CHECK(editable == std::vector<std::string>({"# **标题😀**  ##", "中文 &amp; *斜体*\r\n第二行", "Setext\r\n---", "- item", "last"}));
  CHECK(!ParseMarkdown("plain").TopLevelSourceRange(0));
  Throws<std::out_of_range>([] { (void)MarkdownDocument{}.TopLevelSourceRange(0); });
  for (const auto sample : {"#\n\nlast", "**\ninside\n**\n\nnext", "[x]: /url\n\n[x]\n", "\xEF\xBB\xBF# heading\n\nbody", "$x$\n\nnext", "\\\nnext", "`a\nb`\n\nnext", "foo\n===\n", "a\0b"})
    Verify(sample, ParseMarkdown(sample, options), options);
  const std::string structure = "- **same**\r\n  - **same**\r\n- [x] 完成\r\n\r\n> ## 内题 ##\r\n\r\n"
      "| 左 | 右 |\r\n| --- | ---: |\r\n| A\\|B |  |\r\n| padded |\r\n\r\n```cpp\r\n\r\ncode\r\n```\r\n";
  const auto structured = ParseMarkdown(structure, {.source_ranges = true});
  CHECK(structured.Root() == ParseMarkdown(structure).Root());
  const auto slice = [&](auto path) {
    const auto range = structured.SourceRange(path);
    CHECK(range.has_value());
    return structure.substr(range->start_byte, range->end_byte - range->start_byte);
  };
  CHECK(slice(std::array<std::size_t, 3>{0, 0, 0}) == "**same**");
  CHECK(slice(std::array<std::size_t, 5>{0, 0, 1, 0, 0}) == "**same**");
  CHECK(slice(std::array<std::size_t, 3>{0, 1, 0}) == "完成");
  CHECK(slice(std::array<std::size_t, 2>{1, 0}) == "内题");
  CHECK(slice(std::array<std::size_t, 4>{2, 1, 0, 0}) == "A\\|B");
  CHECK(slice(std::array<std::size_t, 4>{2, 1, 0, 1}).empty());
  CHECK(!structured.SourceRange(std::array<std::size_t, 4>{2, 1, 1, 1}));
  CHECK(slice(std::array<std::size_t, 1>{3}) == "```cpp\r\n\r\ncode\r\n```");
  const auto empty_markers = ParseMarkdown("-\n- item\n-\n\n> [!NOTE]\n> first\n> second\n", {
      .admonitions = true, .source_ranges = true});
  CHECK(empty_markers.TopLevelSourceRange(0) == (MarkdownSourceRange{0, 10}));
  const auto admonition_range = empty_markers.TopLevelSourceRange(1);
  CHECK(admonition_range && admonition_range->start_byte == 12);
  const auto multiline = empty_markers.SourceRange(std::array<std::size_t, 2>{1, 0});
  CHECK(multiline && multiline->end_byte > multiline->start_byte);
  const ParseOptions footnotes{.footnotes = true};
  auto with_locations = footnotes;
  with_locations.source_ranges = true;
  const std::string note = "a[^note]\n\n[^note]: footnote **body**\n";
  const auto footnoted = ParseMarkdown(note, with_locations);
  CHECK(footnoted.Root() == ParseMarkdown(note, footnotes).Root());
  const auto body = footnoted.SourceRange(std::array<std::size_t, 3>{1, 0, 0});
  CHECK(body && note.substr(body->start_byte, body->end_byte - body->start_byte) == "footnote **body**");
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
    std::vector<std::size_t> path;
    VerifyNestedRanges(next, full, full.Root(), path);
    Verify(edited, next, options);
  }
}); }
