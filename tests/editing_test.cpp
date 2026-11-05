#include <huxerui/markdown.h>
#include "test.h"

using namespace huxerui;
using namespace huxerui::markdown;

int main() { return Run([] {
  auto value = TextEditingValue::FromText("A😀B");
  value.selection = {1, 3};
  const auto bold = ApplyMarkdownCommand(value, MarkdownEditCommand::Bold);
  CHECK(bold.text == "A**😀**B");
  CHECK(bold.selection == (TextSelection{3, 5}));
  CHECK(value.text == "A😀B");
  value.selection = {3, 1, TextAffinity::Upstream};
  const auto backward = ApplyMarkdownCommand(value, MarkdownEditCommand::Italic);
  CHECK(backward.text == "A*😀*B");
  CHECK(backward.selection == (TextSelection{4, 2, TextAffinity::Upstream}));
  value.selection = {1, 1};
  const auto empty = ApplyMarkdownCommand(value, MarkdownEditCommand::Bold);
  CHECK(empty.text == "A****😀B");
  CHECK(empty.selection == (TextSelection{3, 3}));
  for (const auto& text : {std::string("`x``y`"), std::string(" spaced "), std::string("   ")}) {
    auto code = TextEditingValue::FromText(text);
    code.selection = {0, AttributedText(text).Length()};
    const auto result = ApplyMarkdownCommand(code, MarkdownEditCommand::InlineCode);
    CHECK(ParseMarkdown(result.text).PlainText() == text);
  }
  auto lines = TextEditingValue::FromText("one\r\ntwo\r\n");
  lines.selection = {0, 5};
  const auto heading = ApplyMarkdownCommand(lines, MarkdownEditCommand::Heading1);
  CHECK(heading.text == "# one\r\ntwo\r\n");
  CHECK(heading.selection == (TextSelection{2, 7}));
  lines.selection = {0, 10};
  const auto list = ApplyMarkdownCommand(lines, MarkdownEditCommand::BulletList);
  CHECK(list.text == "- one\r\n- two\r\n");
  CHECK(ApplyMarkdownCommand(list, MarkdownEditCommand::BulletList) == list);
  const auto quoted = ApplyMarkdownCommand(lines, MarkdownEditCommand::BlockQuote);
  CHECK(quoted.text == "> one\r\n> two\r\n");
  CHECK(ApplyMarkdownCommand(quoted, MarkdownEditCommand::BlockQuote) == quoted);
  auto old_heading = TextEditingValue::FromText("### Old");
  old_heading.selection = {0, 0};
  CHECK(ApplyMarkdownCommand(old_heading, MarkdownEditCommand::Heading2).text == "## Old");
  CHECK(ApplyMarkdownCommand(TextEditingValue::FromText(""), MarkdownEditCommand::Heading1).text == "# ");
  auto newline = TextEditingValue::FromText("a\nb");
  newline.selection = {0, 3};
  Throws<std::invalid_argument>([&] { (void)ApplyMarkdownCommand(newline, MarkdownEditCommand::InlineCode); });
  value.selection = {2, 2};
  Throws<std::invalid_argument>([&] { (void)ApplyMarkdownCommand(value, MarkdownEditCommand::Bold); });
  value.selection = {-1, 0};
  Throws<std::invalid_argument>([&] { (void)ApplyMarkdownCommand(value, MarkdownEditCommand::Bold); });
  value.selection = {0, 99};
  Throws<std::invalid_argument>([&] { (void)ApplyMarkdownCommand(value, MarkdownEditCommand::Bold); });
  value = TextEditingValue::FromText("中文");
  value.composition = TextRange{0, 2};
  Throws<std::logic_error>([&] { (void)ApplyMarkdownCommand(value, MarkdownEditCommand::Bold); });
  CHECK(value.composition == (TextRange{0, 2}));
  value.composition.reset();
  Throws<std::invalid_argument>([&] { (void)ApplyMarkdownCommand(value, static_cast<MarkdownEditCommand>(99)); });
}); }
