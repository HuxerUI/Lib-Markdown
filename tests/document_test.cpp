#include <huxerui/markdown/document.h>
#include "test.h"

using namespace huxerui::markdown;

const MarkdownNode& Find(const MarkdownNode& node, NodeKind kind) {
  if (node.kind == kind) return node;
  for (const auto& child : node.children) {
    try { return Find(child, kind); } catch (const std::out_of_range&) {}
  }
  throw std::out_of_range("Node not found");
}

int main() { return Run([] {
  const auto empty = ParseMarkdown("");
  CHECK(empty.Root().children.empty());
  CHECK(empty.PlainText().empty());
  const auto headings = ParseMarkdown("# One\n\nTwo\n---\n\n###### Six\n");
  CHECK(headings.Root().children.size() == 3);
  CHECK(headings.Root().children[0].heading_level == 1);
  CHECK(headings.Root().children[1].heading_level == 2);
  CHECK(headings.Root().children[2].heading_level == 6);
  CHECK(headings.PlainText() == "One\nTwo\nSix");

  const auto rich = ParseMarkdown("**strong *emphasis*** ~~gone~~ `a * b` \\*literal\\*");
  CHECK(PlainText(Find(rich.Root(), NodeKind::Strong)) == "strong emphasis");
  CHECK(PlainText(Find(rich.Root(), NodeKind::Emphasis)) == "emphasis");
  CHECK(PlainText(Find(rich.Root(), NodeKind::Strikethrough)) == "gone");
  CHECK(Find(rich.Root(), NodeKind::Code).text == "a * b");
  CHECK(rich.PlainText().ends_with("*literal*"));

  const auto lists = ParseMarkdown("3. outer **bold**\n   - inner\n\n- [x] done\n- [ ] pending\n");
  const auto& ordered = Find(lists.Root(), NodeKind::OrderedList);
  CHECK(ordered.list_start == 3);
  CHECK(ordered.children[0].children[0].kind == NodeKind::Paragraph);
  CHECK(PlainText(ordered.children[0].children[0]) == "outer bold");
  CHECK(Find(ordered, NodeKind::UnorderedList).children.size() == 1);
  const auto& tasks = lists.Root().children.back();
  CHECK(tasks.children[0].task_checked == true);
  CHECK(tasks.children[1].task_checked == false);

  const auto code = ParseMarkdown("```cpp title\nint x = 1;\n```\n\n    indented\n");
  CHECK(code.Root().children[0].language == "cpp");
  CHECK(code.Root().children[0].fence_info == "cpp title");
  CHECK(code.Root().children[0].text == "int x = 1;\n");
  CHECK(code.Root().children[1].text == "indented\n");
  const auto unfinished = ParseMarkdown("```cpp\nunfinished");
  CHECK(unfinished.Root().children[0].text == "unfinished\n");

  const auto table = ParseMarkdown("| Left | Center | Right |\n| :--- | :---: | ---: |\n| a | **b** | c |\n");
  const auto& cells = Find(table.Root(), NodeKind::TableRow).children;
  CHECK(cells.size() == 3);
  CHECK(cells[0].alignment == CellAlignment::Left);
  CHECK(cells[1].alignment == CellAlignment::Center);
  CHECK(cells[2].alignment == CellAlignment::Right);
  CHECK(PlainText(Find(table.Root(), NodeKind::Strong)) == "b");

  const auto links = ParseMarkdown("[a **bold**](../guide.md \"A &amp; B\") ![alt *text*](pic.png)\n\n[ref][r]\n\n[r]: https://example.com/?a=1&amp;b=2\n");
  const auto& link = Find(links.Root(), NodeKind::Link);
  CHECK(link.destination == "../guide.md");
  CHECK(link.title == "A & B");
  CHECK(PlainText(link) == "a bold");
  CHECK(Find(links.Root(), NodeKind::Image).destination == "pic.png");
  CHECK(PlainText(Find(links.Root(), NodeKind::Image)) == "alt text");
  CHECK(Find(links.Root().children[1], NodeKind::Link).destination == "https://example.com/?a=1&b=2");
  const auto auto_link = ParseMarkdown("https://example.com");
  CHECK(Find(auto_link.Root(), NodeKind::Link).destination == "https://example.com");

  CHECK(ParseMarkdown("&amp; &copy; &#128512; &#x1F600; &NotEqualTilde; &unknown;").PlainText() ==
      "& \xC2\xA9 \xF0\x9F\x98\x80 \xF0\x9F\x98\x80 \xE2\x89\x82\xCC\xB8 &unknown;");
  CHECK(ParseMarkdown("&#0; &#xD800; &#x110000;").PlainText() == "\xEF\xBF\xBD \xEF\xBF\xBD \xEF\xBF\xBD");
  CHECK(ParseMarkdown(std::string("a\0b", 3)).PlainText() == "a\xEF\xBF\xBD" "b");
  CHECK(ParseMarkdown("<script>alert(1)</script>").PlainText() == "<script>alert(1)</script>");
  CHECK(ParseMarkdown("one\ntwo  \nthree").PlainText() == "one two\nthree");
  CHECK(ParseMarkdown("one\ntwo", {.soft_break_mode = SoftBreakMode::LineBreak}).PlainText() == "one\ntwo");
  CHECK(ParseMarkdown("one\ntwo", {.soft_break_mode = SoftBreakMode::Space}).PlainText() == "one two");
  CHECK(ParseMarkdown("one  \ntwo", {.soft_break_mode = SoftBreakMode::Space}).PlainText() == "one\ntwo");
  CHECK(ParseMarkdown("```\none\ntwo\n```", {.soft_break_mode = SoftBreakMode::Space}).PlainText() == "one\ntwo\n");
  CHECK(ParseMarkdown("`one\ntwo`", {.soft_break_mode = SoftBreakMode::LineBreak}).PlainText() == "one two");
  Throws<std::invalid_argument>([] { (void)ParseMarkdown("", {.soft_break_mode = static_cast<SoftBreakMode>(99)}); });
  CHECK(ParseMarkdown("\xE4\xB8\xAD\xE6\x96\x87 **\xF0\x9F\x98\x80**").PlainText() == "\xE4\xB8\xAD\xE6\x96\x87 \xF0\x9F\x98\x80");
  CHECK(ParseMarkdown("first\r\n\r\nsecond\r\n").PlainText() == "first\nsecond");
  CHECK(ParseMarkdown("> quote\n>\n> - item\n\n---").Root().children.back().kind == NodeKind::ThematicBreak);

  const auto commonmark = ParseMarkdown("~~literal~~", {.strikethrough = false});
  CHECK(commonmark.PlainText() == "~~literal~~");
  CHECK(ParseMarkdown("- [x] literal", {.task_lists = false}).PlainText() == "[x] literal");
  const auto no_tables = ParseMarkdown("a | b\n--- | ---\nx | y", {.tables = false});
  CHECK(no_tables.Root().children.front().kind != NodeKind::Table);
  const auto no_auto = ParseMarkdown("https://example.com", {.autolinks = false});
  CHECK(no_auto.Root().children.front().children.front().kind == NodeKind::Text);

  Throws<std::invalid_argument>([] { (void)ParseMarkdown("\xC0\xAF"); });
  Throws<std::invalid_argument>([] { (void)ParseMarkdown("\xF0\x9F"); });
  Throws<std::invalid_argument>([] { (void)ParseMarkdown("\xED\xA0\x80"); });
  Throws<std::invalid_argument>([] { (void)ParseMarkdown("\xF4\x90\x80\x80"); });
  Throws<std::invalid_argument>([] { (void)ParseMarkdown("x", {.max_nesting = 0}); });
  Throws<std::invalid_argument>([] { (void)ParseMarkdown("x", {.max_nesting = 513}); });
  Throws<std::length_error>([] { (void)ParseMarkdown("long", {.max_input_bytes = 3}); });
  Throws<std::length_error>([] { (void)ParseMarkdown("> > > > nested", {.max_nesting = 2}); });
  CHECK(ParseMarkdown("ok", {.max_input_bytes = 2}).PlainText() == "ok");
  CHECK(ParseMarkdown(std::string(10000, '*')).Root().children.size() == 1);

  for (const auto value : {"https://example.com", "HTTPS://example.com", "mailto:a@b.com", "../guide.md", "#heading", "/docs", "?q=x"})
    CHECK(IsDefaultLinkAllowed(value));
  for (const auto value : {"", "javascript:alert(1)", "JaVaScRiPt:alert(1)", "data:text/html,x", "file:///etc/passwd", "//example.com", "\\evil", "https://a\nb", " javascript:x", "custom:value"})
    CHECK(!IsDefaultLinkAllowed(value));

  MarkdownDocument retained;
  { std::string source = "# Owned\n\n[link](https://example.com)"; retained = ParseMarkdown(source); }
  CHECK(retained.PlainText() == "Owned\nlink");
  const auto again = ParseMarkdown("# Owned\n\n[link](https://example.com)");
  CHECK(retained.Root() == again.Root());
  CHECK(retained != again);
  const auto copy = retained;
  CHECK(copy == retained);
  CHECK(&copy.Root() == &retained.Root());
  retained = MarkdownDocument{};
  CHECK(retained.Root().kind == NodeKind::Document);
  CHECK(retained.Root().children.empty());
  CHECK(retained.PlainText().empty());
  CHECK(copy.PlainText() == "Owned\nlink");
  auto before_move = copy;
  auto moved = std::move(before_move);
  CHECK(moved == copy);
  CHECK(before_move == copy);
  before_move = ParseMarkdown("replacement");
  CHECK(moved.PlainText() == "Owned\nlink");
  auto move_assigned = ParseMarkdown("discarded");
  move_assigned = std::move(moved);
  CHECK(move_assigned == copy);
  CHECK(moved == copy);
  MarkdownDocument survivor;
  { auto owner = ParseMarkdown("# Retained tree"); survivor = owner; }
  CHECK(survivor.PlainText() == "Retained tree");
}); }
