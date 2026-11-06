#include "../src/source_editor.h"
#include "../../../tests/test.h"

#include <huxerui/testing/ui_test.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace huxerui;
using namespace huxerui::testing;

State<TextEditingValue> source_value;
std::string initial_source;
std::vector<TextEditingValue> source_changes;

std::string MakeSource() {
  std::string source;
  for (int line = 0; line < 180; ++line) {
    source += "行 " + std::to_string(line) + " ";
    for (int column = 0; column < 18; ++column) source += "中文😀source ";
    source += line % 2 == 0 ? "\r\n" : "\n";
  }
  return source;
}

View SourceEditorTestApp() {
  auto value = UseState(TextEditingValue::FromText(initial_source));
  source_value = value;
  return markdown_writer::LargeSourceEditor(value.Get(), {
      .changed = [value](const TextEditingValue& next) mutable {
        source_changes.push_back(next);
        value = next;
      },
  }).Key("writer-source").With(Grow{});
}

} // namespace

int main() {
  return Run([] {
    initial_source = MakeSource();
    source_changes.clear();
    CHECK(initial_source.size() > 3 * 16 * 1024);

    Application application(SourceEditorTestApp, {.show_debug_overlay = false});
    UiTest ui(application, {.viewport = {900, 700}});

    auto list = ui.Find(UiSelector::Key("writer-source-list"));
    auto first = ui.Find(UiSelector::Key("writer-source-chunk-1"));
    const std::string original_first = first.One().value.value_or("");
    CHECK(!original_first.empty());
    CHECK(original_first.size() <= 16 * 1024 + 1);
    CHECK(original_first.back() == '\n');
    CHECK(original_first.find("\r\n") != std::string::npos);
    CHECK(initial_source.starts_with(original_first));
    CHECK(source_value.Get().text == initial_source);
    const auto first_identity = first.One().key;

    // Local insertion preserves all original bytes and maps the emitted caret
    // from the chunk to the complete source in UTF-16 units.
    first.Tap();
    first.SetSelection({0, 0});
    const std::size_t changes_before_insert = source_changes.size();
    first.EnterText("A😀");
    std::string expected = "A😀" + initial_source;
    CHECK(source_changes.size() == changes_before_insert + 1);
    CHECK(source_value.Get().text == expected);
    CHECK(source_changes.back().selection == (TextSelection{3, 3}));
    CHECK(first.One().value == "A😀" + original_first);
    CHECK(first.One().key == first_identity);

    // Newlines grow the current chunk without rechanging its identity or
    // splitting the retained source into a new set of fields.
    ui.PressKey(Key::Enter);
    expected.insert(5, "\n");
    CHECK(source_value.Get().text == expected);
    CHECK(first.One().value->starts_with("A😀\n"));
    CHECK(first.One().key == first_identity);

    auto second = list.ScrollUntil(UiSelector::Key("writer-source-chunk-2"), {
        .step = {0, 500},
        .maximum_steps = 100,
    });
    const auto second_identity = second.One().key;
    CHECK(second_identity != first_identity);
    const std::string second_text = second.One().value.value_or("");
    const std::size_t second_byte_offset = original_first.size() + 6;
    CHECK(expected.substr(second_byte_offset, second_text.size()) == second_text);
    const TextOffset second_utf16_offset = AttributedText(original_first).Length() + 4;

    second.Tap();
    auto cross_chunk_selection = source_value.Get();
    cross_chunk_selection.selection = {
        0,
        second_utf16_offset + 1,
        TextAffinity::Upstream,
    };
    source_value = cross_chunk_selection;
    ui.Pump();
    CHECK(ui.ActiveTextInput().has_value());
    CHECK(ui.ActiveTextInput()->selection == (TextSelection{
        0,
        1,
        TextAffinity::Upstream,
    }));
    CHECK(second.One().key == second_identity);

    second.SetSelection({0, 1});
    CHECK(source_value.Get().text == expected);
    CHECK(source_changes.back().selection == (TextSelection{
        second_utf16_offset,
        second_utf16_offset + 1,
    }));

    // IME updates and commits remain local to this field while their public
    // value uses complete-source UTF-16 offsets.
    second.SetSelection({0, 0});
    const auto session = ui.ActiveTextInput();
    CHECK(session.has_value());
    CHECK(ui.SendTextInput({session->session_id, {{
        .kind = TextInputCommandKind::UpdateComposition,
        .text = "拼",
    }}}).result_code == TextInputResultCode::Ok);
    ui.Pump();
    expected.insert(second_byte_offset, "拼");
    CHECK(source_value.Get().text == expected);
    CHECK(source_changes.back().selection == (TextSelection{
        second_utf16_offset + 1,
        second_utf16_offset + 1,
    }));
    CHECK(source_changes.back().composition == (TextRange{
        second_utf16_offset,
        second_utf16_offset + 1,
    }));
    CHECK(second.One().value->starts_with("拼"));
    CHECK(second.One().key == second_identity);

    CHECK(ui.SendTextInput({session->session_id, {{
        .kind = TextInputCommandKind::CommitText,
        .text = "中文",
    }}}).result_code == TextInputResultCode::Ok);
    ui.Pump();
    expected.replace(second_byte_offset, std::string("拼").size(), "中文");
    CHECK(source_value.Get().text == expected);
    CHECK(source_changes.back().selection == (TextSelection{
        second_utf16_offset + 2,
        second_utf16_offset + 2,
    }));
    CHECK(!source_changes.back().composition.has_value());
    CHECK(second.One().value->starts_with("中文"));
    CHECK(second.One().key == second_identity);

    // A genuinely external replacement gets fresh identities and does not
    // echo another editing callback into the controller.
    const std::size_t changes_before_replace = source_changes.size();
    auto replacement = TextEditingValue::FromText("外部\r\n替换😀\n");
    replacement.selection = {2, 4};
    source_value = replacement;
    ui.Pump();
    CHECK(source_changes.size() == changes_before_replace);
    auto replaced = ui.Find(UiSelector::Type<TextField>());
    const auto replaced_info = replaced.One();
    CHECK(replaced_info.value == replacement.text);
    CHECK(replaced_info.key != first_identity);
    CHECK(replaced_info.key != second_identity);
    const auto replacement_identity = replaced_info.key;

    // Selection-only external updates retain the replacement's native field.
    replaced.Tap();
    replacement.selection = {5, 2, TextAffinity::Upstream};
    source_value = replacement;
    ui.Pump();
    CHECK(ui.ActiveTextInput().has_value());
    CHECK(ui.ActiveTextInput()->selection == replacement.selection);
    CHECK(replaced.One().key == replacement_identity);
    CHECK(replaced.One().value == replacement.text);

    source_value = TextEditingValue::FromText("");
    ui.Pump();
    CHECK(ui.Find(UiSelector::Type<TextField>()).Count() == 1);
    CHECK(ui.Find(UiSelector::Type<TextField>()).One().value == "");
    CHECK(ui.Find(UiSelector::Type<TextField>()).One().key != replacement_identity);
  });
}
