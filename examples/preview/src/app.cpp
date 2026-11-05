#include "app.h"
#include "samples.h"

#include <huxerui/markdown.h>
#include <exception>
#include <cstdint>
#include <sstream>

namespace markdown_preview {
using namespace huxerui;
using namespace huxerui::markdown;

TextEditingValue SourceValue(const std::string& source) {
  auto value = TextEditingValue::FromText(source);
  value.selection = {0, 0};
  return value;
}

View CodeCard(const MarkdownNode& code, const ThemeSpec& theme) {
  std::vector<View> lines;
  std::istringstream input(code.text);
  std::string line;
  unsigned number = 1;
  while (std::getline(input, line)) {
    lines.push_back(Row{
      Text(std::to_string(number++)).Style({Font::Monospace(13), theme.colors.on_surface_variant})
          .With(Frame{.width = 28.0F}),
      Text(line.empty() ? " " : line).Style({Font::Monospace(14), theme.colors.on_surface}).With(Grow{}),
    }.With(Spacing(12.0F)));
  }
  return Column{
    Text(code.language.empty() ? "plain text" : code.language).With(Foreground{theme.colors.primary}),
    Column(std::move(lines)).With(Spacing(4.0F), CrossAlign(CrossAxisAlignment::Stretch)),
  }.With(Padding(16.0F), Spacing(12.0F), Background{theme.colors.surface_container_highest},
         Border{theme.colors.outline, 1.0F}, CornerRadius(10.0F)).Key("custom-code");
}

View LocalImage(const MarkdownNode& image, const ThemeSpec& theme) {
  if (image.destination != "demo:landscape")
    return Text(PlainText(image)).With(Foreground{theme.colors.on_surface_variant});
  return Column{
    Canvas([theme](PaintContext& paint, Size size) {
      paint.DrawRect({0, 0, size.width, size.height}, theme.colors.primary_container, CornerRadii{10});
      paint.DrawCircle({size.width * 0.72F, size.height * 0.3F}, 30.0F, theme.colors.primary);
      paint.DrawRect({size.width * 0.08F, size.height * 0.65F, size.width * 0.84F, size.height * 0.26F},
                     theme.colors.secondary, CornerRadii{20});
      paint.DrawRect({size.width * 0.2F, size.height * 0.5F, size.width * 0.42F, size.height * 0.3F},
                     theme.colors.on_primary_container, CornerRadii{30});
    }).With(Frame{.height = 160.0F}).Key("custom-image"),
    Text(PlainText(image)).With(Foreground{theme.colors.on_surface_variant}),
  }.With(Spacing(8.0F), CrossAlign(CrossAxisAlignment::Stretch));
}

View Panel(std::string title, View content, const ThemeSpec& theme) {
  return Column{
    Text(std::move(title)).Style({Font::System(16).WithWeight(FontWeight::SemiBold), theme.colors.on_surface}),
    std::move(content).With(Grow{}),
  }.With(Padding(16.0F), Spacing(12.0F), CrossAlign(CrossAxisAlignment::Stretch),
         Background{theme.colors.surface}, Border{theme.colors.outline, 0.5F}, CornerRadius(12.0F));
}

View App() {
  static const auto initial_document = ParseMarkdown(Samples()[0].source);
  auto selected = UseState(std::size_t{0});
  auto dark = UseState(false);
  auto show_source = UseState(true);
  auto custom = UseState(false);
  auto editor = UseState(SourceValue(Samples()[0].source));
  auto document = UseState(initial_document);
  auto message = UseState(std::string("选择样例，或修改源码后点击渲染。链接地址会显示在这里。"));

  return MaterialTheme(dark.Get() ? MaterialDarkThemeSpec() : MaterialLightThemeSpec(), Scope([=] {
    const auto theme = UseTheme();
    const bool compact = UseViewportClass() == ViewportClass::Compact;
    std::vector<View> samples;
    for (std::size_t i = 0; i < Samples().size(); ++i) {
      samples.push_back(Chip(Samples()[i].name, selected.Get() == i)
          .OnChanged([=](bool) mutable {
            selected = i;
            editor = SourceValue(Samples()[i].source);
            document = ParseMarkdown(Samples()[i].source);
            custom = i == 3;
            message = "已载入：" + Samples()[i].name;
          }).Key("sample-" + std::to_string(i)));
    }
    MarkdownOptions options;
    options.on_link = [message](const std::string& link) mutable { message = "链接：" + link; };
    if (custom.Get()) {
      options.code_renderer = [theme](const MarkdownNode& code) { return CodeCard(code, theme); };
      options.image_renderer = [theme](const MarkdownNode& image) { return LocalImage(image, theme); };
    }

    View preview = Panel("渲染预览", ScrollView(Markdown(document.Get(), std::move(options)))
        .With(ScrollBar{}).Key("preview-scroll"), theme)
        .Key(reinterpret_cast<std::uintptr_t>(document.Get().get()));
    View body = std::move(preview);
    if (show_source.Get()) {
      View source = Panel("Markdown 源码", TextField(editor)
          .LineLimits(TextFieldLineLimits::MultiLine(1))
          .VerticalAlign(TextVerticalAlign::Top)
          .OnChanged([editor](const TextEditingValue& next) mutable { editor = next; })
          .Key("source-editor"), theme);
      body = compact ? View(Column{std::move(source).With(Grow{}), std::move(body).With(Grow{})}
                                   .With(Spacing(12.0F), CrossAlign(CrossAxisAlignment::Stretch)))
                     : View(Row{std::move(source).With(Grow{0.9F}), std::move(body).With(Grow{1.3F})}
                                .With(Spacing(16.0F), CrossAlign(CrossAxisAlignment::Stretch)));
    }

    return Column{
      Row{
        Text("Markdown Playground").Style({Font::System(compact ? 22.0F : 28.0F).WithWeight(FontWeight::Bold), theme.colors.on_surface})
            .With(Grow{}),
        Button(dark.Get() ? "浅色主题" : "深色主题")
            .OnClick([dark]() mutable { dark = !dark.Get(); }).Key("theme-toggle"),
      }.With(Spacing(12.0F), CrossAlign(CrossAxisAlignment::Center)),
      Text("HuxerUI 原生 Markdown · 编辑、预览与组件扩展示例")
          .With(Foreground{theme.colors.on_surface_variant}),
      Flow(std::move(samples)).With(Spacing(8.0F)),
      Flow{
        Button("渲染").OnClick([=]() mutable {
          try {
            document = ParseMarkdown(editor.Get().text);
            message = "已渲染：" + std::to_string(editor.Get().text.size()) + " 字节";
          } catch (const std::exception& error) { message = std::string("无法解析：") + error.what(); }
        }).Key("render"),
        Button("重置样例").OnClick([=]() mutable {
          editor = SourceValue(Samples()[selected.Get()].source);
          document = ParseMarkdown(Samples()[selected.Get()].source);
          message = "已重置当前样例";
        }).Key("reset"),
        Button(show_source.Get() ? "隐藏源码" : "显示源码")
            .OnClick([show_source]() mutable { show_source = !show_source.Get(); }).Key("source-toggle"),
        Checkbox("自定义渲染", custom.Get())
            .OnChanged([custom](bool enabled) mutable { custom = enabled; }).Key("custom-toggle"),
      }.With(Spacing(8.0F)),
      std::move(body).With(Grow{}),
      Text(message).With(Foreground{theme.colors.on_surface_variant}).Key("status"),
    }.With(Padding(compact ? 12.0F : 24.0F), Spacing(12.0F), CrossAlign(CrossAxisAlignment::Stretch),
           Background{theme.colors.background});
  }));
}

} // namespace markdown_preview

const huxerui::Application application{
  markdown_preview::App,
  {.window = {.title = "HuxerUI Markdown Playground", .initial_size = {1180.0F, 820.0F}}, .show_debug_overlay = false},
};
