#include <huxerui/markdown/document.h>
#include "test.h"
#include <algorithm>
#include <array>
#include <random>

using namespace huxerui::markdown;

ParseOptions Extensions() {
  return {.highlight = true, .insertions = true, .underline = true,
          .wiki_links = true, .admonitions = true, .footnotes = true};
}

std::vector<const MarkdownNode*> Nodes(const MarkdownNode& root, NodeKind kind) {
  std::vector<const MarkdownNode*> result;
  if (root.kind == kind) result.push_back(&root);
  for (const auto& child : root.children) {
    const auto nested = Nodes(child, kind);
    result.insert(result.end(), nested.begin(), nested.end());
  }
  return result;
}

int main() { return Run([] {
  CHECK(ParseOptions{} == ParseOptions{});
  const auto options = Extensions();
  CHECK(options != ParseOptions{});
  CHECK(Nodes(ParseMarkdown("==x==", {.highlight = true}).Root(), NodeKind::Highlight).size() == 1);
  CHECK(Nodes(ParseMarkdown("++x++", {.insertions = true}).Root(), NodeKind::Insertion).size() == 1);
  CHECK(Nodes(ParseMarkdown("_x_", {.underline = true}).Root(), NodeKind::Underline).size() == 1);
  CHECK(Nodes(ParseMarkdown("[[x]]", {.wiki_links = true}).Root(), NodeKind::WikiLink).size() == 1);
  CHECK(Nodes(ParseMarkdown("> [!NOTE]\n> x", {.admonitions = true}).Root(), NodeKind::Admonition).size() == 1);
  CHECK(Nodes(ParseMarkdown("x[^a]\n\n[^a]: body", {.footnotes = true}).Root(), NodeKind::FootnoteReference).size() == 1);
  const auto rich = ParseMarkdown("==bright **bold**== ++added++ _under_ *em* **strong**", options);
  CHECK(rich.PlainText() == "bright bold added under em strong");
  for (const auto kind : {NodeKind::Highlight, NodeKind::Insertion, NodeKind::Underline, NodeKind::Emphasis})
    CHECK(Nodes(rich.Root(), kind).size() == 1);
  CHECK(Nodes(rich.Root(), NodeKind::Strong).size() == 2);
  CHECK(Nodes(ParseMarkdown("_em_ __bold__").Root(), NodeKind::Underline).empty());
  CHECK(Nodes(ParseMarkdown("_em_ __bold__").Root(), NodeKind::Emphasis).size() == 1);
  CHECK(Nodes(ParseMarkdown("==x== ++y++ [[page]]").Root(), NodeKind::Highlight).empty());
  CHECK(ParseMarkdown("==x== ++y++ [[page]]").PlainText() == "==x== ++y++ [[page]]");
  const auto literal = ParseMarkdown("`==x== ++y++ [[page]]`\n\n```\n==x== ++y++\n```\n\n\\=\\=escaped\\=\\=", options);
  CHECK(Nodes(literal.Root(), NodeKind::Highlight).empty());
  CHECK(Nodes(literal.Root(), NodeKind::WikiLink).empty());
  const auto wiki = ParseMarkdown("[[文档/开始|项目文档]] [[plain]] [[a&amp;b|A &amp; B]]", options);
  const auto wiki_nodes = Nodes(wiki.Root(), NodeKind::WikiLink);
  CHECK(wiki_nodes.size() == 3);
  CHECK(wiki_nodes[0]->destination == "文档/开始");
  CHECK(PlainText(*wiki_nodes[0]) == "项目文档");
  CHECK(wiki_nodes[1]->destination == "plain");
  CHECK(PlainText(*wiki_nodes[1]) == "plain");
  CHECK(wiki_nodes[2]->destination == "a&b");
  CHECK(PlainText(*wiki_nodes[2]) == "A & B");
  const std::array<std::string, 5> labels{"NOTE", "TIP", "IMPORTANT", "WARNING", "CAUTION"};
  const std::array<AdmonitionKind, 5> kinds{AdmonitionKind::Note, AdmonitionKind::Tip,
      AdmonitionKind::Important, AdmonitionKind::Warning, AdmonitionKind::Caution};
  for (std::size_t i = 0; i < labels.size(); ++i) {
    const auto source = "> [!" + labels[i] + "]\n> **body**\n>\n> - nested\n";
    const auto doc = ParseMarkdown(source, options);
    CHECK(doc.Root().children[0].kind == NodeKind::Admonition);
    CHECK(doc.Root().children[0].admonition_kind == kinds[i]);
    CHECK(Nodes(doc.Root(), NodeKind::Strong).size() == 1);
    CHECK(Nodes(doc.Root(), NodeKind::UnorderedList).size() == 1);
    CHECK(ParseMarkdown(source).Root().children[0].kind == NodeKind::BlockQuote);
  }
  CHECK(ParseMarkdown("> [!UNKNOWN]\n> body", options).Root().children[0].kind == NodeKind::BlockQuote);
  const auto notes = ParseMarkdown("B[^二] A[^一] again[^二]\n\n[^一]: first\n\n[^二]: second **bold**\n\n[^unused]: invisible\n", options);
  const auto references = Nodes(notes.Root(), NodeKind::FootnoteReference);
  const auto definitions = Nodes(notes.Root(), NodeKind::FootnoteDefinition);
  CHECK(references.size() == 3);
  CHECK(definitions.size() == 2);
  CHECK(references[0]->footnote_number == 1 && references[0]->footnote_reference == 1);
  CHECK(references[0]->footnote_label == "二");
  CHECK(references[1]->footnote_number == 2);
  CHECK(references[2]->footnote_number == 1 && references[2]->footnote_reference == 2);
  CHECK(definitions[0]->footnote_number == 1 && definitions[0]->footnote_label == "二");
  CHECK(definitions[0]->footnote_reference_count == 2);
  CHECK(definitions[1]->footnote_reference_count == 1);
  CHECK(notes.PlainText() == "B[1] A[2] again[1]\nsecond bold\nfirst");
  CHECK(Nodes(ParseMarkdown("x[^missing]", options).Root(), NodeKind::FootnoteReference).empty());
  CHECK(Nodes(ParseMarkdown("x[^a]\n\n[^a]: body").Root(), NodeKind::FootnoteSection).empty());

  const std::vector<std::string> seeds{
    "# First\n\n==highlight== ++insert++ _underline_\n\n# Later\n\nplain\n",
    "# First\n\n[[页|**label**]]\n\n# Later\n\nplain\n",
    "# First\n\n> [!NOTE]\n> ==body==\n>\n> - nested\n\n# Later\n\nplain\n",
    "# First\n\nref[^a]\n\n# Later\n\n[^a]: body\n\n    continuation\n",
    "# First\r\n\r\n> [!WARNING]\r\n> body\r\n\r\n# Later\r\n\r\nplain\r\n",
    "# First\n\n| a | b |\n|---|---|\n| ==x== | [[页]] |\n\n# Later\n\nplain\n",
  };
  const std::vector<std::string> fragments{"=", "+", "_", "*", "\n", "\n\n", "==新==", "[[页|别名]]",
      "> [!TIP]\n> tip\n", "[^a]", "[^a]: definition\n", "`", "```\n", "# Heading\n", "", "中文😀"};
  std::mt19937 random(101);
  for (const auto& seed : seeds) {
    MarkdownParseSession session(options);
    auto text = seed;
    (void)session.Update(text);
    for (int i = 0; i < 150; ++i) {
      std::vector<std::size_t> boundaries;
      for (std::size_t byte = 0; byte < text.size(); ++byte)
        if ((static_cast<unsigned char>(text[byte]) & 0xC0) != 0x80) boundaries.push_back(byte);
      boundaries.push_back(text.size());
      auto first = random() % boundaries.size();
      auto last = std::min<std::size_t>(first + random() % 5, boundaries.size() - 1);
      const auto& inserted = fragments[random() % fragments.size()];
      const MarkdownEdit edit{boundaries[first], boundaries[last] - boundaries[first], inserted};
      text.replace(edit.start_byte, edit.removed_bytes, inserted);
      CHECK(session.ApplyEdit(edit).Root() == ParseMarkdown(text, options).Root());
    }
    MarkdownStream stream(options);
    for (const char byte : seed) {
      (void)stream.Append(std::string_view(&byte, 1));
      CHECK(stream.Document().Root() == ParseMarkdown(stream.Source(), options).Root());
    }
    CHECK(stream.Finish().Root() == ParseMarkdown(seed, options).Root());
  }
  MarkdownParseSession incremental(options);
  (void)incremental.Update(seeds[0]);
  const auto updated = incremental.Append("tail\n");
  CHECK(incremental.LastParseStats().mode == MarkdownParseMode::Incremental);
  CHECK(updated.Root() == ParseMarkdown(std::string(incremental.Source()), options).Root());
  MarkdownParseSession global(options);
  (void)global.Update("ref[^a]\n\n# Later\n\nbody\n");
  const auto resolved = global.Append("\n[^a]: defined at end\n");
  CHECK(global.LastParseStats().mode == MarkdownParseMode::Full);
  CHECK(Nodes(resolved.Root(), NodeKind::FootnoteReference).size() == 1);
  CHECK(global.Update("ref[^a]\n\n# Later\n\nbody\n").Root() == ParseMarkdown("ref[^a]\n\n# Later\n\nbody\n", options).Root());
  CHECK(global.LastParseStats().mode == MarkdownParseMode::Full);
}); }
