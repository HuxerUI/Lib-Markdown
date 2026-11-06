#include "editor.h"

#include <huxerui/huxerui.h>

using namespace huxerui;

namespace {

View App() {
  std::vector<AttributedText> paragraphs{
    AttributedText{TextSpan("Welcome ").Style({.font_weight = FontWeight::Bold}), TextSpan("to rich text.")},
    AttributedText("Drag across these paragraphs, then type to replace the selection."),
    AttributedText("试试跨段选择、中文输入和撤销。"),
  };
  for (unsigned i = 1; i <= 60; ++i) {
    paragraphs.emplace_back("Editable paragraph " + std::to_string(i));
  }
  return MaterialTheme(Column {
    Text("Rich Text Editor", TextRole::Title),
    Text("Select text and press Ctrl+B or Command+B to toggle bold."),
    rich_text::RichTextEditor(std::move(paragraphs)).With(Grow{}),
  }.With(Padding(24.0F), Spacing(8.0F), CrossAlign(CrossAxisAlignment::Stretch)));
}

const Application application{
  App,
  {.window = {.title = "Rich Text Editor", .initial_size = {720, 560}}},
};

} // namespace

int main() { return RunApplication(); }
