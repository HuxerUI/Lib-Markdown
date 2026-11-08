#include <huxerui/markdown/document.h>
#include "test.h"

#include <random>
#include <algorithm>
#include <vector>

using namespace huxerui::markdown;

void Equivalent(MarkdownParseSession& session, const std::string& source, ParseOptions options = {}) {
  const auto actual = session.Update(source);
  const auto expected = ParseMarkdown(source, options);
  if (!(actual.Root() == expected.Root()))
    throw std::runtime_error("Incremental/full mismatch for source:\n" + source);
  CHECK(actual.Root() == expected.Root());
  CHECK(session.Source() == source);
  CHECK(session.Document() == actual);
}

std::vector<std::size_t> Boundaries(const std::string& text) {
  std::vector<std::size_t> result;
  for (std::size_t i = 0; i < text.size(); ++i)
    if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) result.push_back(i);
  result.push_back(text.size());
  return result;
}

int main() { return Run([] {
  MarkdownParseSession session;
  CHECK(session.Source().empty());
  CHECK(session.Document().Root().children.empty());
  CHECK(session.LastParseStats().mode == MarkdownParseMode::Unchanged);
  std::string large;
  for (int i = 0; i < 120; ++i)
    large += "## Section " + std::to_string(i) + "\n\nParagraph **bold** and `code`.\n\n";
  Equivalent(session, large);
  CHECK(session.LastParseStats().mode == MarkdownParseMode::Full);
  const auto old = session.Document();
  const auto old_text = old.PlainText();
  large += "New tail text\n";
  Equivalent(session, large);
  CHECK(session.LastParseStats().mode == MarkdownParseMode::Incremental);
  CHECK(session.LastParseStats().reused_blocks > 200);
  CHECK(session.LastParseStats().parsed_bytes < large.size() / 10);
  CHECK(old.PlainText() == old_text);
  const auto unchanged = session.Document();
  CHECK(session.Update(large) == unchanged);
  CHECK(session.LastParseStats().parsed_bytes == 0);
  CHECK(session.LastParseStats().mode == MarkdownParseMode::Unchanged);

  const auto changed = session.ApplyEdit({large.size() - 5, 4, "tail 😀"});
  large.replace(large.size() - 5, 4, "tail 😀");
  CHECK(changed.Root() == ParseMarkdown(large).Root());
  CHECK(session.LastParseStats().mode == MarkdownParseMode::Incremental);

  const std::vector<std::string> seeds{
    "# Start\n\nAlpha\nsecond line\n\n## Next\n\nBeta\n",
    "plain\n\nsecond paragraph\ncontinuation\n\nthird paragraph\n",
    "# Before\n\n```cpp\n# not a heading\n\nnot a restart\n```\n\n# After\n\ntext\n",
    "# Before\n\n~~~\nunterminated\n\n# still code\n",
    "# Before\n\n- a\n\n  - nested\n    continuation\n\n# After\n\ntext\n",
    "# Before\n\n> quote\n>\n> nested\n\nplain\n\n# After\n\ntext\n",
    "# Before\n\n| a | b |\n|---|---|\n| c | d |\n\n# After\n\ntext\n",
    "# Before\n\n[earlier][label]\n\n# After\n\n[label]: https://example.com\n",
    "# Before\n\n[empty]()\nlater text\n\n# After\n\ntext\n",
    "# Before\n\n*emphasis\ncontinues*\n\nsetext\n---\n\ntext\n",
    "# 中文\r\n\r\n段落 😀\r\n\r\n## 后续\r\n\r\n结束\r\n",
    "# Before\n\n\xEF\xBB\xBF" "literal BOM\n\n# After\n\ntext\n",
    "# Before\r\rplain\r\r# After\r\rtext\r",
    std::string("# Before\n\na") + std::string(1, '\0') + "b\n\n# After\n\ntext\n",
  };
  const std::vector<std::string> fragments{
    "x", "\n", "\n\n", "# Heading\n", "```\n", "~~~\n", "- item\n", "> quote\n", "    indent\n",
    "[label]: /target\n", "[a\nb]: /url\n", "[label]", "[x](url)", "*", "_", "`", "|---|\n", "\r\n",
    "\xEF\xBB\xBF", "中文😀", "", std::string(1, '\0'),
  };
  std::mt19937 random(0x48555845);
  for (int variant = 0; variant < 3; ++variant) {
    ParseOptions options;
    if (variant == 1) options.soft_break_mode = SoftBreakMode::LineBreak;
    if (variant == 2) { options.tables = false; options.autolinks = {false, false, false}; options.task_lists = false; }
    for (const auto& seed : seeds) {
      MarkdownParseSession parser(options);
      auto text = seed;
      Equivalent(parser, text, options);
      for (int edit = 0; edit < 100; ++edit) {
        const auto positions = Boundaries(text);
        const auto first = random() % positions.size();
        const auto last = std::min<std::size_t>(positions.size() - 1, first + random() % 8);
        const auto& inserted = fragments[random() % fragments.size()];
        const MarkdownEdit replacement{positions[first], positions[last] - positions[first], inserted};
        text.replace(replacement.start_byte, replacement.removed_bytes, inserted);
        const auto actual = parser.ApplyEdit(replacement);
        CHECK(actual.Root() == ParseMarkdown(text, options).Root());
        CHECK(parser.Source() == text);
      }
    }
  }

  // These edits keep the early structure intact, exercising repeated suffix parses.
  for (const auto& seed : seeds) {
    MarkdownParseSession parser;
    auto text = seed;
    Equivalent(parser, text);
    for (const auto& fragment : fragments) {
      text += fragment;
      Equivalent(parser, text);
    }
  }

  MarkdownParseSession references;
  Equivalent(references, "[ref][x]\n\n# Later\n\nbody\n");
  Equivalent(references, "[ref][x]\n\n# Later\n\nbody\n\n[x]: /resolved\n");
  CHECK(references.LastParseStats().mode == MarkdownParseMode::Full);
  CHECK(references.Document().Root().children[0].children[0].kind == NodeKind::Link);
  Equivalent(references, "[ref][x]\n\n# Later\n\nbody\n");
  CHECK(references.LastParseStats().mode == MarkdownParseMode::Full);

  MarkdownParseSession limits({.max_input_bytes = 24, .max_nesting = 3});
  limits.Update("# A\n\n中文😀\n");
  const auto before = limits.Document();
  const auto stats = limits.LastParseStats();
  const auto source = std::string(limits.Source());
  Throws<std::out_of_range>([&] { limits.ApplyEdit({999, 0, ""}); });
  Throws<std::out_of_range>([&] { limits.ApplyEdit({0, 999, ""}); });
  Throws<std::invalid_argument>([&] { limits.ApplyEdit({7, 0, "x"}); });
  Throws<std::invalid_argument>([&] { limits.ApplyEdit({5, 1, "x"}); });
  Throws<std::invalid_argument>([&] { limits.ApplyEdit({0, 0, "\xF0\x9F"}); });
  Throws<std::length_error>([&] { limits.Update(std::string(25, 'a')); });
  Throws<std::length_error>([&] { limits.ApplyEdit({0, 0, std::string(25, 'a')}); });
  Throws<std::length_error>([&] { limits.Update("> > > > nested"); });
  CHECK(limits.Document() == before);
  CHECK(limits.Source() == source);
  CHECK(limits.LastParseStats() == stats);
  Throws<std::invalid_argument>([] { MarkdownParseSession invalid({.max_input_bytes = 0}); });

  MarkdownDocument retained;
  { MarkdownParseSession temporary; retained = temporary.Update("retained"); }
  CHECK(retained.PlainText() == "retained");
}); }
