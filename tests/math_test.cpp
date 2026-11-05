#include <huxerui/markdown/document.h>
#include "test.h"
#include <algorithm>
#include <random>

using namespace huxerui::markdown;

std::vector<const MarkdownNode*> FindNodes(const MarkdownNode& root, NodeKind kind) {
  std::vector<const MarkdownNode*> result;
  if (root.kind == kind) result.push_back(&root);
  for (const auto& child : root.children) {
    const auto nested = FindNodes(child, kind);
    result.insert(result.end(), nested.begin(), nested.end());
  }
  return result;
}

int main() { return Run([] {
  const auto math = ParseMarkdown("before $x^2 + **literal** &amp; \\alpha$ after\n\n$$\\frac{a}{b}$$", {.math = true});
  const auto inline_nodes = FindNodes(math.Root(), NodeKind::InlineMath);
  const auto display_nodes = FindNodes(math.Root(), NodeKind::DisplayMath);
  CHECK(inline_nodes.size() == 1 && display_nodes.size() == 1);
  CHECK(inline_nodes[0]->text == "x^2 + **literal** &amp; \\alpha");
  CHECK(inline_nodes[0]->children.empty());
  CHECK(display_nodes[0]->text == "\\frac{a}{b}");
  CHECK(math.PlainText() == "before x^2 + **literal** &amp; \\alpha after\n\\frac{a}{b}");
  CHECK(FindNodes(ParseMarkdown("$x$ $$y$$").Root(), NodeKind::InlineMath).empty());
  const auto literal = ParseMarkdown("`$code$`\n\n```\n$$not math$$\n```\n\n\\$escaped\\$", {.math = true});
  CHECK(FindNodes(literal.Root(), NodeKind::InlineMath).empty());
  CHECK(FindNodes(literal.Root(), NodeKind::DisplayMath).empty());
  const auto multiline = ParseMarkdown("$$\na + b\n$$", {.math = true});
  CHECK(FindNodes(multiline.Root(), NodeKind::DisplayMath).size() == 1);
  CHECK(FindNodes(multiline.Root(), NodeKind::DisplayMath)[0]->text.find("a + b") != std::string::npos);
  CHECK(ParseMarkdown("$unclosed", {.math = true}).PlainText() == "$unclosed");

  const auto blanks = ParseMarkdown("a\n\n\nb", {.preserve_blank_lines = true});
  CHECK(blanks.Root().children.size() == 3);
  CHECK(blanks.Root().children[1].kind == NodeKind::BlankLines);
  CHECK(blanks.Root().children[1].blank_line_count == 2);
  CHECK(blanks.PlainText() == "a\n\n\nb");
  CHECK(ParseMarkdown("a\n\n\nb").PlainText() == "a\nb");
  CHECK(ParseMarkdown("\n\nA\n\n", {.preserve_blank_lines = true}).PlainText() == "\n\nA\n\n");
  const auto only_blank = ParseMarkdown("\n\n", {.preserve_blank_lines = true});
  CHECK(only_blank.Root().children.size() == 1);
  CHECK(only_blank.Root().children[0].blank_line_count == 2);
  CHECK(only_blank.PlainText() == "\n\n");
  CHECK(FindNodes(ParseMarkdown("```\n\n\n```", {.preserve_blank_lines = true}).Root(), NodeKind::BlankLines).empty());

  const std::vector<std::string> seeds{
    "# First\n\nalpha $x^2$\n\n\n# Next\n\nlast\n",
    "\n\n# First\n\n$$\\frac{a}{b}$$\n\n# Next\n\n中文😀\n\n",
    "# First\n\n- a\n\n  - b\n\n> quote\n>\n> $x$\n\n# Next\n\nlast\n",
    "# First\n\n```\n\n$x$\n\n```\n\n# Next\n\nlast\n",
    "# First\r\n\r\n$x$\r\n\r\n\r\n# Next\r\n\r\nlast\r\n",
    "# First\n\nref[^a]\n\n# Next\n\n[^a]: $x$\n\n    more\n",
  };
  const std::vector<std::string> fragments{"$", "$$", "$x$", "$$\n", "\n", "\n\n\n", "# H\n", "```\n", "- a\n",
      "> q\n", "[^a]: $y$\n", "中文😀", "", "\\frac{a}{b}", "*", "\r\n"};
  std::mt19937 random(0x4D415448);
  for (int variant = 0; variant < 4; ++variant) {
    ParseOptions options{.highlight = true, .insertions = true, .underline = true,
        .wiki_links = true, .admonitions = true, .footnotes = true,
        .math = (variant & 1) != 0, .preserve_blank_lines = (variant & 2) != 0};
    for (const auto& seed : seeds) {
      MarkdownParseSession session(options);
      auto text = seed;
      (void)session.Update(text);
      for (int i = 0; i < 100; ++i) {
        std::vector<std::size_t> positions;
        for (std::size_t j = 0; j < text.size(); ++j)
          if ((static_cast<unsigned char>(text[j]) & 0xC0) != 0x80) positions.push_back(j);
        positions.push_back(text.size());
        const auto first = random() % positions.size();
        const auto last = std::min<std::size_t>(positions.size() - 1, first + random() % 6);
        const auto& insert = fragments[random() % fragments.size()];
        const MarkdownEdit edit{positions[first], positions[last] - positions[first], insert};
        text.replace(edit.start_byte, edit.removed_bytes, insert);
        CHECK(session.ApplyEdit(edit).Root() == ParseMarkdown(text, options).Root());
      }
      MarkdownStream stream(options);
      for (const char byte : seed) {
        (void)stream.Append(std::string_view(&byte, 1));
        CHECK(stream.Document().Root() == ParseMarkdown(stream.Source(), options).Root());
      }
      CHECK(stream.Finish().Root() == ParseMarkdown(seed, options).Root());
    }
  }
  MarkdownParseSession incremental({.math = true, .preserve_blank_lines = true});
  (void)incremental.Update(seeds[0]);
  const auto changed = incremental.Append("\n\n$x+1$\n");
  CHECK(incremental.LastParseStats().mode == MarkdownParseMode::Incremental);
  CHECK(changed.Root() == ParseMarkdown(incremental.Source(), {.math = true, .preserve_blank_lines = true}).Root());
  MarkdownStream incomplete({.math = true});
  (void)incomplete.Append("$not closed");
  CHECK(incomplete.Finish().PlainText() == "$not closed"); // Finish does not invent a closing delimiter.
}); }
