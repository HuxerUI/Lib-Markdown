#include <huxerui/markdown.h>
#include "test.h"
using namespace huxerui;
using namespace huxerui::markdown;

const ParseOptions options{.highlight = true, .insertions = true, .underline = true, .wiki_links = true};
MarkdownRichText Rich(std::string_view source) {
  auto document = ParseMarkdown(source, options);
  return MarkdownRichText(document.Root().children.at(0), options);
}
void RoundTrip(const MarkdownRichText& text) {
  const auto source = text.SourceValue();
  CHECK(ParseMarkdown(source.text, options).PlainText() == text.Value().text);
}
int main() {
  return Run([] {
    for (auto source :
         {"# heading **bold**", "plain **bold** *italic*", "**outer *nested***", "==marked== ++inserted++ __under__",
          "a [link](../target.md \"title\") b", "[[page|label]]", "`back` ticks", "&amp; 中文😀"}) {
      std::string stage = "initial";
      try {
        auto text = Rich(source);
        RoundTrip(text);
        auto value = text.Value();
        value.text += "追加😀";
        value.selection = TextEditingValue::FromText(value.text).selection;
        stage = "append";
        text.Update(value);
        RoundTrip(text);
        value = text.Value();
        value.text = " replace * literal &amp; [x] ";
        value.selection = TextEditingValue::FromText(value.text).selection;
        stage = "replace";
        text.Update(value);
        RoundTrip(text);
      } catch (const std::exception& error) {
        throw std::runtime_error(std::string(source) + " (" + stage + "): " + error.what());
      }
    }
    auto text = Rich("before **中文😀** after");
    auto value = text.Value();
    value.text = "before 中文😀粗体 after";
    value.selection = {13, 13};
    text.Update(value);
    CHECK(text.SourceValue().text == "before **中文😀粗体** after");
    const auto before = text.Value();
    value.selection = {10, 10}; // splits emoji surrogate pair
    Throws<std::invalid_argument>([&] { text.Update(value); });
    CHECK(text.Value() == before);
    auto mixed = Rich("plain **bold** *italic*");
    const auto original = mixed.Value();
    auto composing = TextEditingValue::FromText("拼音");
    composing.composition = TextRange{0, 2};
    mixed.Update(composing);
    mixed.Update(original);
    CHECK(mixed.SourceValue().text == "plain **bold** *italic*");
    auto code = Rich("`x`");
    const auto old = code.Value();
    Throws<std::invalid_argument>([&] { code.Update(TextEditingValue::FromText("x\ny")); });
    CHECK(code.Value() == old);
    auto repeated = Rich("**a**a");
    auto caret = repeated.Value();
    caret.selection = {1, 1};
    repeated.Update(caret);
    caret.text = "aaa";
    caret.selection = {2, 2};
    repeated.Update(caret);
    CHECK(repeated.SourceValue().text == "**aa**a");
    repeated = Rich("**a**a");
    caret = repeated.Value();
    caret.selection = {1, 1};
    repeated.Update(caret);
    caret.text = "a";
    caret.selection = {0, 0};
    repeated.Update(caret);
    CHECK(repeated.SourceValue().text == "a");
    auto undo = Rich("plain **bold** *italic*");
    auto saved = undo.Value();
    auto empty = TextEditingValue::FromText("");
    undo.Update(empty);
    undo.Update(saved);
    CHECK(undo.SourceValue().text == "plain **bold** *italic*");
  });
}
