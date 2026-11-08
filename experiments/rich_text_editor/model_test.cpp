#include "rich_text_model.h"
#include "test.h"

#include <array>

using namespace huxerui;
using namespace huxerui::rich_text::detail;

class TestClipboard final : public PlatformClipboard {
public:
  std::optional<std::string> ReadText() override { return text; }
  bool WriteText(std::string_view value) override { text = std::string(value); return true; }
  std::optional<std::string> text;
};

std::string PlainTextOf(const EditSession& session) {
  const auto& document = session.Current().document;
  return document.TextInRange({0, document.Length()});
}

int main() {
  return Run([] {
    const auto bold = TextSpanStyle{.font_weight = FontWeight::Bold};
    const auto emphasized = AttributedText::FromRanges("A😀B", {{{1, 3}, bold}});
    const auto following = AttributedText("tail");
    EditSession session({emphasized, AttributedText("CD"), following});
    const auto before = session.Current().document;
    const TextSelection spanning{6, 1, TextAffinity::Upstream};
    session.SetSelection(spanning);
    Throws<std::invalid_argument>([&] { session.Replace({2, 3}, "invalid"); });
    CHECK(session.Current().document == before);
    CHECK(session.Current().selection == spanning);

    session.UpdateComposition(spanning.Range(), "拼");
    CHECK(PlainTextOf(session) == "A拼D\ntail");
    CHECK(session.Composition() == (TextRange{1, 2}));
    CHECK(session.Current().document.Paragraphs().front().id == before.Paragraphs().front().id);
    CHECK(session.Current().document.Paragraphs().back().id == before.Paragraphs().back().id);
    CHECK(&session.Current().document.Paragraphs().back().text.PlainText() == &following.PlainText());
    session.UpdateComposition(*session.Composition(), "中文");
    CHECK(PlainTextOf(session) == "A中文D\ntail");
    CHECK(session.Current().document.Paragraphs().front().text.StyleRanges().front().range == (TextRange{1, 3}));
    CHECK(session.Current().document.Paragraphs().front().text.StyleRanges().front().style == bold);
    session.FinishComposition();
    CHECK(session.Undo());
    CHECK(session.Current().document == before);
    CHECK(session.Current().selection == spanning);
    CHECK(!session.Undo());
    CHECK(session.Redo());
    CHECK(PlainTextOf(session) == "A中文D\ntail");
    const auto accepted = session.Current();
    session.BeginComposition({1, 3});
    session.UpdateComposition({2, 3}, "🙂");
    CHECK(PlainTextOf(session) == "A中🙂D\ntail");
    Throws<std::invalid_argument>([&] { session.UpdateComposition({0, 2}, "invalid"); });
    CHECK(session.CancelComposition());
    CHECK(session.Current().document == accepted.document);
    CHECK(session.Current().selection == accepted.selection);

    session.Replace({1, 1}, "\n\n");
    CHECK(PlainTextOf(session) == "A\n\n中文D\ntail");
    CHECK(session.Current().document.Paragraphs().size() == 4);
    const auto split_id = session.Current().document.Paragraphs()[1].id;
    CHECK(session.Undo());
    session.Replace({1, 1}, "\n");
    CHECK(session.Current().document.Paragraphs()[1].id != split_id);
    CHECK(!session.Redo());

    const auto colored = AttributedText::FromRanges("abc", {{{1, 3}, {.foreground = Color::White()}}},
        {{{1, 3}, Uri("https://example.com")}});
    EditSession formatting({colored, AttributedText("def")});
    formatting.SetSelection({2, 6});
    formatting.SetBold(true);
    const auto& first = formatting.Current().document.Paragraphs().front().text;
    CHECK(first.StyleRanges().size() == 2);
    CHECK(first.StyleRanges()[0].style.foreground == Color::White());
    CHECK(first.StyleRanges()[1].style.font_weight == FontWeight::Bold);
    CHECK(first.LinkRanges().size() == 1);
    CHECK(formatting.Current().document.Paragraphs().back().text.StyleRanges().front().range == (TextRange{0, 2}));
    CHECK(formatting.Undo());
    CHECK(formatting.Current().document.Paragraphs().front().text == colored);
    CHECK(formatting.Current().selection == (TextSelection{2, 6}));

    EditSession batched({AttributedText("head"), AttributedText("tail")});
    batched.SetSelection({2, 7});
    const auto batch_baseline = batched.Current();
    const std::array invalid_batch{
      TextInputCommand{.kind = TextInputCommandKind::CommitText, .text = "bad"},
      TextInputCommand{.kind = TextInputCommandKind::SetSelection, .selection_after = TextSelection{99, 99}},
    };
    Throws<std::invalid_argument>([&] { batched.ApplyCommands(invalid_batch); });
    CHECK(batched.Current().document == batch_baseline.document);
    CHECK(batched.Current().selection == batch_baseline.selection);
    CHECK(!batched.Undo());
    const std::array compose{
      TextInputCommand{.kind = TextInputCommandKind::UpdateComposition, .text = "拼"},
      TextInputCommand{.kind = TextInputCommandKind::CommitText, .text = "中文"},
    };
    batched.ApplyCommands(compose);
    CHECK(PlainTextOf(batched) == "he中文il");
    CHECK(batched.Undo());
    CHECK(batched.Current().document == batch_baseline.document);
    CHECK(batched.Current().selection == batch_baseline.selection);
    CHECK(!batched.Undo());

    EditSession deleting({AttributedText("A😀B")});
    deleting.SetSelection({3, 3});
    auto delete_before = TextInputCommand{.kind = TextInputCommandKind::DeleteSurrounding, .delete_before = 1};
    Throws<std::invalid_argument>([&] { deleting.ApplyCommands(std::span(&delete_before, 1)); });
    CHECK(PlainTextOf(deleting) == "A😀B");
    delete_before.delete_unit = TextInputUnit::UnicodeCodePoint;
    deleting.ApplyCommands(std::span(&delete_before, 1));
    CHECK(PlainTextOf(deleting) == "AB");
    CHECK(deleting.Undo());
    CHECK(PlainTextOf(deleting) == "A😀B");

    EditSession outside_composition({AttributedText("aBc")});
    outside_composition.SetSelection({1, 2});
    outside_composition.UpdateComposition({1, 2}, "拼");
    outside_composition.SetSelection({1, 1});
    outside_composition.ApplyCommands(std::span(&delete_before, 1));
    CHECK(PlainTextOf(outside_composition) == "拼c");
    CHECK(outside_composition.Composition() == (TextRange{0, 1}));
    CHECK(outside_composition.CancelComposition());
    CHECK(PlainTextOf(outside_composition) == "Bc");
    CHECK(outside_composition.Undo());
    CHECK(PlainTextOf(outside_composition) == "aBc");

    TestClipboard clipboard;
    EditSession clipboard_edit({AttributedText("First"), AttributedText("Second")});
    clipboard_edit.SetSelection({2, 8});
    CHECK(clipboard_edit.Perform(TextEditingAction::Copy, &clipboard));
    CHECK(clipboard.text == "rst\nSe");
    CHECK(clipboard_edit.Perform(TextEditingAction::Cut, &clipboard));
    CHECK(PlainTextOf(clipboard_edit) == "Ficond");
    CHECK(clipboard_edit.Undo());
    CHECK(clipboard_edit.Current().selection == (TextSelection{2, 8}));
    clipboard.text = "中\r\n文";
    CHECK(clipboard_edit.Perform(TextEditingAction::Paste, &clipboard));
    CHECK(PlainTextOf(clipboard_edit) == "Fi中\n文cond");
    CHECK(clipboard_edit.Undo());
    CHECK(PlainTextOf(clipboard_edit) == "First\nSecond");
  });
}
