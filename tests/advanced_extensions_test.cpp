#include <huxerui/markdown/document.h>
#include "test.h"
#include <random>
using namespace huxerui::markdown;
unsigned Count(const MarkdownNode& node, NodeKind kind) {
  unsigned count = node.kind == kind;
  for (const auto& child : node.children) count += Count(child, kind);
  return count;
}
int main() { return Run([] {
  CHECK(Count(ParseMarkdown("before ||**secret**|| after").Root(), NodeKind::Spoiler) == 0);
  const auto spoiler = ParseMarkdown("before ||**secret** [link](../a)|| after", {.spoilers = true});
  CHECK(Count(spoiler.Root(), NodeKind::Spoiler) == 1);
  CHECK(Count(spoiler.Root(), NodeKind::Strong) == 1);
  CHECK(spoiler.PlainText() == "before secret link after");
  CHECK(Count(ParseMarkdown("`||literal||`\n\n```\n||code||\n```", {.spoilers = true}).Root(), NodeKind::Spoiler) == 0);
  const auto common = ParseOptions::CommonMark();
  CHECK(!common.tables && !common.strikethrough && !common.task_lists);
  CHECK((common.autolinks == MarkdownAutolinkOptions{false, false, false}));
  CHECK(ParseOptions::GitHub().admonitions && ParseOptions::GitHub().footnotes);
  const std::string links = "https://example.com a@example.com www.example.com <https://explicit.example>";
  CHECK(Count(ParseMarkdown(links, common).Root(), NodeKind::Link) == 1);
  CHECK(Count(ParseMarkdown(links).Root(), NodeKind::Link) == 4);
  for (const auto enabled : {MarkdownAutolinkOptions{true, false, false}, {false, true, false}, {false, false, true}})
    CHECK(Count(ParseMarkdown(links, {.autolinks = enabled}).Root(), NodeKind::Link) == 2);
  CHECK(ParseMarkdown("#nospace").Root().children.front().kind == NodeKind::Paragraph);
  CHECK(ParseMarkdown("#nospace", {.permissive_atx_headings = true}).Root().children.front().kind == NodeKind::Heading);
  CHECK(ParseMarkdown("    indented").Root().children.front().kind == NodeKind::CodeBlock);
  CHECK(ParseMarkdown("    indented", {.indented_code_blocks = false}).Root().children.front().kind == NodeKind::Paragraph);
  const auto collapsed = ParseMarkdown("a   b\t c", {.collapse_whitespace = true}).PlainText();
  if (collapsed != "a b c") throw std::runtime_error("Collapsed text: [" + collapsed + "]");
  CHECK(ParseMarkdown("`a   b`", {.collapse_whitespace = true}).PlainText() == "a   b");
  std::mt19937 random(931);
  const std::vector<std::string> tokens{"a", "\n", "\n\n", "#nospace", "||secret||", "[x]: /url\n", "    ", "\t", "`", "www.a.com", "**"};
  for (int variant = 0; variant < 6; ++variant) {
    auto options = variant == 0 ? ParseOptions::CommonMark() : ParseOptions::GitHub();
    options.spoilers = true;
    options.collapse_whitespace = variant & 1;
    options.permissive_atx_headings = variant & 2;
    options.indented_code_blocks = !(variant & 4);
    MarkdownParseSession parser(options);
    std::string text = "# title\n\nbefore ||secret||\n\nlast\n";
    for (int n = 0; n < 100; ++n) {
      text.insert(random() % (text.size() + 1), tokens[random() % tokens.size()]);
      const auto doc = parser.Update(text);
      CHECK(doc.Root() == ParseMarkdown(text, options).Root());
    }
    MarkdownStream stream(options);
    for (const char c : text) stream.Append(std::string_view(&c, 1));
    CHECK(stream.Finish().Root() == ParseMarkdown(text, options).Root());
  }
}); }
