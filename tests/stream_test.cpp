#include <huxerui/markdown/document.h>
#include "test.h"
#include <algorithm>
#include <random>
#include <vector>

using namespace huxerui::markdown;

int main() { return Run([] {
  const std::vector<std::string> sources{
    "", "# 中文 😀\n\n**粗体**\n\n```cpp\nint n;\n```\n",
    "# A\n\n- [ ] task\n  - nested\n\n> quote\n\n| a | b |\n|---|---|\n| x | y |\n",
    "[early][ref]\n\n# Later\n\n[ref]: ../文档.md\n",
    "# A\r\n\r\n&copy; *open emphasis\n\n~~~\nopen code",
    std::string("\xEF\xBB\xBF# BOM\n\na") + '\0' + "b\n",
  };
  for (const auto& source : sources) {
    // Every possible two-chunk split, including each byte within Unicode scalars.
    for (std::size_t split = 0; split <= source.size(); ++split) {
      MarkdownStream stream;
      stream.Append(std::string_view(source).substr(0, split));
      CHECK(stream.Document().Root() == ParseMarkdown(stream.Source()).Root());
      stream.Append(std::string_view(source).substr(split));
      CHECK(stream.PendingByteCount() == 0);
      CHECK(stream.Source() == source);
      const auto final = stream.Finish();
      CHECK(final.Root() == ParseMarkdown(source).Root());
      CHECK(stream.IsFinished());
      CHECK(stream.Finish() == final);
      Throws<std::logic_error>([&] { stream.Append("x"); });
    }
    for (int variant = 0; variant < 3; ++variant) {
      ParseOptions options;
      if (variant == 1) options.soft_break_mode = SoftBreakMode::LineBreak;
      if (variant == 2) { options.tables = false; options.autolinks = {false, false, false}; }
      MarkdownStream stream(options);
      std::mt19937 random(42);
      for (std::size_t offset = 0; offset < source.size();) {
        const auto count = std::min<std::size_t>(variant == 0 ? 1 : 1 + random() % 9, source.size() - offset);
        stream.Append(std::string_view(source).substr(offset, count));
        offset += count;
        CHECK(stream.Source().size() + stream.PendingByteCount() == offset);
        CHECK(stream.PendingByteCount() <= 3);
        CHECK(stream.Document().Root() == ParseMarkdown(stream.Source(), options).Root());
      }
      CHECK(stream.Finish().Root() == ParseMarkdown(source, options).Root());
    }
  }

  MarkdownStream stream;
  const auto old = stream.Append("# First\n\nsecond paragraph\n\n");
  stream.Append("\xF0\x9F");
  const auto stats = stream.LastParseStats();
  CHECK(stream.PendingByteCount() == 2);
  CHECK(stream.Document() == old);
  Throws<std::invalid_argument>([&] { stream.Finish(); });
  Throws<std::invalid_argument>([&] { stream.Append("invalid"); });
  CHECK(!stream.IsFinished());
  CHECK(stream.PendingByteCount() == 2);
  CHECK(stream.Document() == old);
  CHECK(stream.LastParseStats() == stats);
  stream.Append("\x98\x80\n");
  CHECK(stream.Source().ends_with("😀\n"));
  CHECK(stream.LastParseStats().mode == MarkdownParseMode::Incremental);
  CHECK(old.PlainText() == "First\nsecond paragraph");
  const auto current = stream.Document();
  stream.Append("");
  CHECK(stream.Document() == current);
  for (const auto& invalid : std::vector<std::string>{"\x80", "\xC0", "\xED\xA0", "\xE0\x80", "\xF4\x90", "\xF0\x80"}) {
    Throws<std::invalid_argument>([&] { stream.Append(invalid); });
    CHECK(stream.Document() == current);
    CHECK(stream.PendingByteCount() == 0);
  }
  MarkdownStream limited({.max_input_bytes = 4});
  limited.Append("a\xF0\x9F");
  Throws<std::length_error>([&] { limited.Append("\x98\x80"); });
  CHECK(limited.Source() == "a");
  CHECK(limited.PendingByteCount() == 2);
  MarkdownStream depth({.max_nesting = 2});
  const auto before = depth.Append("# A\n\n");
  Throws<std::length_error>([&] { depth.Append("> > > x"); });
  CHECK(depth.Document() == before);
  CHECK(depth.Source() == "# A\n\n");
  MarkdownParseSession session;
  CHECK(session.Append("hello").PlainText() == "hello");
  CHECK(session.Append(" world").PlainText() == "hello world");
}); }
