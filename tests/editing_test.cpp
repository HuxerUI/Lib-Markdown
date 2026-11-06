#include <huxerui/markdown.h>
#include "document_editor_internal.h"
#include "editing_internal.h"
#include "test.h"
#include <array>

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
  MarkdownBlockEditSession session("# keep\r\n\r\n中文😀\r\n\r\n- untouched\r\n");
  const auto base = session.Document();
  session.BeginEdit(1);
  CHECK(session.EditingValue().text == "中文😀");
  auto input = session.EditingValue();
  input.selection = {0, 4};
  session.UpdateEdit(input);
  session.ApplyCommand(MarkdownEditCommand::Bold);
  CHECK(session.Source() == "# keep\r\n\r\n**中文😀**\r\n\r\n- untouched\r\n");
  CHECK(session.LayoutDocument() == base);
  CHECK(session.Document().Root() == ParseMarkdown(session.Source(), {.source_ranges = true}).Root());
  auto composing = TextEditingValue::FromText("输入中😀");
  composing.composition = TextRange{0, 3};
  session.UpdateEdit(composing);
  const auto provisional_source = std::string(session.Source());
  Throws<std::logic_error>([&] { session.EndEdit(); });
  Throws<std::logic_error>([&] { session.BeginEdit(0); });
  Throws<std::logic_error>([&] { session.UpdateSource("replace"); });
  Throws<std::logic_error>([&] { session.ApplyCommand(MarkdownEditCommand::Bold); });
  CHECK(session.Source() == provisional_source && session.EditingValue() == composing);
  composing.selection = {4, 4}; // Inside the emoji surrogate pair.
  Throws<std::invalid_argument>([&] { session.UpdateEdit(composing); });
  CHECK(session.Source() == provisional_source);
  session.UpdateEdit(TextEditingValue::FromText("## changed\n\nnew paragraph\n\n- new list"));
  session.EndEdit();
  CHECK(!session.EditingBlock());
  CHECK(session.LayoutDocument() == session.Document());
  CHECK(session.Source() == "# keep\r\n\r\n## changed\n\nnew paragraph\n\n- new list\r\n\r\n- untouched\r\n");
  const auto stable = std::string(session.Source());
  Throws<std::invalid_argument>([&] { session.BeginEdit(std::span<const std::size_t>{}); });
  CHECK(session.Source() == stable);
  Throws<std::logic_error>([&] { session.UpdateEdit(value); });
  MarkdownBlockEditSession limited("body", {.max_input_bytes = 10});
  limited.BeginEdit(0);
  Throws<std::length_error>([&] { limited.UpdateEdit(TextEditingValue::FromText(std::string(11, 'a'))); });
  CHECK(limited.Source() == "body" && limited.EditingValue().text == "body");
  MarkdownBlockEditSession list_items("3) **first**\r\n   - [x] nested\r\n4) same\r\n");
  list_items.BeginEdit(std::array<std::size_t, 3>{0, 0, 0});
  CHECK(list_items.EditingValue().text == "**first**");
  list_items.UpdateEdit(TextEditingValue::FromText("**first中文😀**"));
  CHECK(list_items.Source() == "3) **first中文😀**\r\n   - [x] nested\r\n4) same\r\n");
  const auto before = list_items.Document();
  Throws<std::invalid_argument>([&] { list_items.UpdateEdit(TextEditingValue::FromText("- another list")); });
  Throws<std::invalid_argument>([&] { list_items.UpdateEdit(TextEditingValue::FromText("a\nb")); });
  CHECK(list_items.Document() == before);
  list_items.BeginEdit(std::array<std::size_t, 5>{0, 0, 1, 0, 0});
  CHECK(list_items.EditingValue().text == "nested");
  list_items.UpdateEdit(TextEditingValue::FromText("child"));
  CHECK(list_items.Source() == "3) **first中文😀**\r\n   - [x] child\r\n4) same\r\n");
  MarkdownBlockEditSession table("| A | B |\r\n| --- | ---: |\r\n| c |  |\r\n");
  table.BeginEdit(std::array<std::size_t, 4>{0, 1, 0, 1});
  CHECK(table.EditingValue().text.empty());
  table.UpdateEdit(TextEditingValue::FromText("中\\|😀"));
  CHECK(table.Source() == "| A | B |\r\n| --- | ---: |\r\n| c |  中\\|😀|\r\n");
  const auto accepted_table = table.Document();
  Throws<std::invalid_argument>([&] { table.UpdateEdit(TextEditingValue::FromText("a|b")); });
  CHECK(table.Document() == accepted_table);

  markdown::detail::MarkdownDocumentEditSession document_edit(
      "# A **bold**\r\n\r\ntail *kept*\r\n\r\n- untouched\r\n");
  const auto baseline = document_edit;
  CHECK(baseline.Document() == document_edit.Document());
  const auto original_list = document_edit.Document().Root().children.back();
  const auto position = document_edit.Replace({0, 3}, {1, 4}, "中文");
  CHECK(position == (markdown::detail::MarkdownBlockPosition{0, 5}));
  CHECK(document_edit.Source() == "# A **b中文** *kept*\r\n\r\n- untouched\r\n");
  CHECK(document_edit.Document().Root().children.back() == original_list);
  CHECK(baseline.Source() == "# A **bold**\r\n\r\ntail *kept*\r\n\r\n- untouched\r\n");

  const ParseOptions blank_lines{.preserve_blank_lines = true};
  markdown::detail::MarkdownDocumentEditSession blank_document("# Title\n\nBody\n", blank_lines);
  CHECK(blank_document.Document().Root().children[1].kind == NodeKind::BlankLines);
  CHECK(blank_document.Replace({0, 2}, {1, 2}, "中文") == (markdown::detail::MarkdownBlockPosition{0, 4}));
  CHECK(blank_document.Source() == "# Ti中文dy\n");

  markdown::detail::MarkdownDocumentEditSession hidden("# A\n\n[id]: https://example.com\n\nBody\n");
  const auto hidden_source = std::string(hidden.Source());
  Throws<std::invalid_argument>([&] { (void)hidden.Replace({0, 1}, {1, 1}, "x"); });
  CHECK(hidden.Source() == hidden_source);

  markdown::detail::MarkdownDocumentEditSession unicode("## A😀\n\nBody\n");
  const auto unicode_snapshot = unicode.Document();
  Throws<std::invalid_argument>([&] { (void)unicode.Replace({0, 2}, {1, 1}, "x"); });
  CHECK(unicode.Document() == unicode_snapshot);
  CHECK(unicode.Source() == "## A😀\n\nBody\n");

  markdown::detail::MarkdownDocumentEditSession mixed("First\n\n- item\n\nLast\n");
  Throws<std::invalid_argument>([&] { (void)mixed.Replace({0, 2}, {2, 1}, "replacement"); });
  CHECK(mixed.Source() == "First\n\n- item\n\nLast\n");

  using markdown::detail::EditableMarkdownDocument;
  using markdown::detail::EditableMarkdownSource;
  const std::string_view markdown_source = "# Title\r\n\r\nBody\r\n\r\n*After*\r\n";
  rich_text::detail::EditSession backed(EditableMarkdownDocument(markdown_source, blank_lines));
  backed.SetSelection({2, 8, TextAffinity::Upstream});
  const auto before_composition = backed.Current();
  const std::array preedit{TextInputCommand{.kind = TextInputCommandKind::UpdateComposition, .text = "拼"}};
  backed.ApplyCommands(preedit);
  CHECK(backed.Composition() == (TextRange{2, 3}));
  CHECK(EditableMarkdownSource(backed.Current().document) == "# Ti拼dy\r\n\r\n*After*\r\n");
  CHECK(backed.CancelComposition());
  CHECK(backed.Current().document == before_composition.document);
  CHECK(backed.Current().selection == before_composition.selection);
  const std::array invalid_batch{preedit.front(),
      TextInputCommand{.kind = TextInputCommandKind::SetSelection, .selection_after = TextSelection{0, 999}}};
  Throws<std::invalid_argument>([&] { backed.ApplyCommands(invalid_batch); });
  CHECK(EditableMarkdownSource(backed.Current().document) == markdown_source);
  CHECK(!backed.Composition());
  backed.ApplyCommands(preedit);
  backed.ApplyCommands(std::array{TextInputCommand{.kind = TextInputCommandKind::CommitText, .text = "中文"}});
  const auto committed = backed.Current().document;
  CHECK(EditableMarkdownSource(committed) == "# Ti中文dy\r\n\r\n*After*\r\n");
  CHECK(committed.TextInRange({0, committed.Length()}) == "Ti中文dy\nAfter");
  CHECK(committed.Paragraphs().front().id == before_composition.document.Paragraphs().front().id);
  CHECK(committed.Paragraphs().back().id == before_composition.document.Paragraphs().back().id);
  CHECK(committed.Paragraphs().back().text == before_composition.document.Paragraphs().back().text);
  const auto style = MarkdownStyle::FromTheme(MaterialDarkThemeSpec());
  const auto displayed = markdown::detail::EditableMarkdownParagraph(committed, 0, style);
  CHECK(displayed.PlainText() == "Ti中文dy");
  CHECK(!displayed.StyleRanges().empty() && displayed.StyleRanges().front().style.font.has_value());
  const auto after_display = markdown::detail::EditableMarkdownParagraph(committed, 1, style);
  CHECK(after_display.StyleRanges().size() == 1 && after_display.StyleRanges().front().style.font &&
        after_display.StyleRanges().front().style.font->Slant() == FontSlant::Italic);
  CHECK(backed.Undo());
  CHECK(EditableMarkdownSource(backed.Current().document) == markdown_source);
  CHECK(backed.Current().selection == before_composition.selection);
  CHECK(backed.Redo());
  CHECK(backed.Current().document == committed);

  const auto title_end = committed.Paragraphs().front().text.Length();
  const auto new_start = title_end + 1;
  backed.Replace({title_end, title_end}, "\n");
  CHECK(EditableMarkdownSource(backed.Current().document) == "# Ti中文dy\r\n\r\n\r\n\r\n*After*\r\n");
  CHECK(backed.Current().document.Paragraphs()[1].text.PlainText().empty());
  CHECK(backed.Current().selection == (TextSelection{new_start, new_start}));
  backed.Replace({new_start, new_start}, "新\n段");
  CHECK(EditableMarkdownSource(backed.Current().document) == "# Ti中文dy\r\n\r\n新\r\n\r\n段\r\n\r\n*After*\r\n");
  CHECK(backed.Current().document.Paragraphs().back().id == committed.Paragraphs().back().id);
  backed.Replace({title_end, new_start}, {});
  CHECK(EditableMarkdownSource(backed.Current().document) == "# Ti中文dy新\r\n\r\n段\r\n\r\n*After*\r\n");

  rich_text::detail::EditSession empty_document(EditableMarkdownDocument({}, blank_lines));
  empty_document.Replace({0, 0}, "😀");
  CHECK(EditableMarkdownSource(empty_document.Current().document) == "😀");
  empty_document.Replace({0, 2}, {});
  CHECK(EditableMarkdownSource(empty_document.Current().document).empty());
  CHECK(empty_document.Current().document.Paragraphs().size() == 1);
}); }
