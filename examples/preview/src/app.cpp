#include "app.h"
#include "samples.h"

#include <huxerui/markdown.h>
#include <exception>
#include <cstdint>
#include <sstream>
#include <chrono>

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

struct PreviewSession {
  MarkdownParseSession parser;
  TaskHandle pending;
  std::uint64_t request = 0;

  void CancelPending() {
    pending.Cancel();
    ++request;
  }
};

std::string DescribeParse(const MarkdownParseStats& stats) {
  const char* mode = stats.mode == MarkdownParseMode::Incremental ? "增量解析" :
      stats.mode == MarkdownParseMode::Full ? "全量解析" : "内容未变化";
  return std::string(mode) + " · 重解析 " + std::to_string(stats.parsed_bytes) +
      " 字节 · 复用 " + std::to_string(stats.reused_blocks) + "/" + std::to_string(stats.total_blocks) + " 块";
}

void PublishPreview(const std::shared_ptr<PreviewSession>& session, const std::string& source,
                    State<MarkdownDocument> document, State<MarkdownParseStats> stats, State<std::string> message) {
  try {
    document = session->parser.Update(source);
    stats = session->parser.LastParseStats();
    message = "已更新预览：" + std::to_string(source.size()) + " 字节";
  } catch (const std::exception& error) {
    message = std::string("无法解析：") + error.what();
  }
}

void RequestPreview(const std::shared_ptr<PreviewSession>& session, TaskScope tasks, std::string source,
                    State<MarkdownDocument> document, State<MarkdownParseStats> stats,
                    State<std::string> message, bool immediate = false) {
  session->CancelPending();
  if (immediate) { PublishPreview(session, source, document, stats, message); return; }
  const auto request = session->request;
  message = "等待实时预览…";
  session->pending = tasks.Launch([=]() -> Task<void> {
    co_await Delay(std::chrono::milliseconds(120));
    if (session->request == request) PublishPreview(session, source, document, stats, message);
  });
}

View App() {
  static const auto initial_document = ParseMarkdown(Samples()[0].source);
  auto selected = UseState(std::size_t{0});
  auto dark = UseState(false);
  auto show_source = UseState(true);
  auto custom = UseState(false);
  auto live = UseState(true);
  auto session = UseState(std::make_shared<PreviewSession>());
  auto tasks = UseTaskScope();
  auto editor = UseState(SourceValue(Samples()[0].source));
  auto document = UseState(initial_document);
  auto document_version = UseState(std::uint64_t{0});
  auto stats = UseState(MarkdownParseStats{});
  auto message = UseState(std::string("实时预览已开启，编辑源码后自动更新。"));
  Lifecycle([=]() mutable {
    (void)session.Get()->parser.Update(Samples()[0].source);
    stats = session.Get()->parser.LastParseStats();
    return [session] { session.Get()->CancelPending(); };
  });

  return MaterialTheme(dark.Get() ? MaterialDarkThemeSpec() : MaterialLightThemeSpec(), Scope([=] {
    const auto theme = UseTheme();
    const bool compact = UseViewportClass() == ViewportClass::Compact;
    std::vector<View> samples;
    for (std::size_t i = 0; i < Samples().size(); ++i) {
      samples.push_back(Chip(Samples()[i].name, selected.Get() == i)
          .OnChanged([=](bool) mutable {
            selected = i;
            editor = SourceValue(Samples()[i].source);
            RequestPreview(session.Get(), tasks, Samples()[i].source, document, stats, message, true);
            document_version = document_version.Get() + 1;
            custom = i == 3;
            message = "已载入：" + Samples()[i].name;
          }).Key("sample-" + std::to_string(i)));
    }
    MarkdownOptions options;
    if (custom.Get()) {
      options.code_block_renderer = [theme](const MarkdownNode& code) { return CodeCard(code, theme); };
      options.standalone_image_renderer = [theme](const MarkdownNode& image) { return LocalImage(image, theme); };
    }

    View preview = Panel("渲染预览", ScrollView(Markdown(document.Get(), std::move(options))
        .On<MarkdownEvents::LinkActivated>([message](const MarkdownLinkActivation& event) mutable {
          message = "链接：" + event.destination;
        }))
        .With(ScrollBar{}).Key("preview-scroll"), theme)
        .Key(document_version.Get());
    View body = std::move(preview);
    if (show_source.Get()) {
      View source = Panel("Markdown 源码", TextField(editor)
          .LineLimits(TextFieldLineLimits::MultiLine(1))
          .VerticalAlign(TextVerticalAlign::Top)
          .OnChanged([=](const TextEditingValue& next) mutable {
            const bool changed = next.text != editor.Get().text;
            editor = next;
            if (!changed) return;
            if (live.Get()) RequestPreview(session.Get(), tasks, next.text, document, stats, message);
            else session.Get()->CancelPending();
          })
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
          RequestPreview(session.Get(), tasks, editor.Get().text, document, stats, message, true);
        }).Key("render"),
        Button("重置样例").OnClick([=]() mutable {
          editor = SourceValue(Samples()[selected.Get()].source);
          RequestPreview(session.Get(), tasks, Samples()[selected.Get()].source, document, stats, message, true);
          document_version = document_version.Get() + 1;
          message = "已重置当前样例";
        }).Key("reset"),
        Button(show_source.Get() ? "隐藏源码" : "显示源码")
            .OnClick([show_source]() mutable { show_source = !show_source.Get(); }).Key("source-toggle"),
        Checkbox("自定义渲染", custom.Get())
            .OnChanged([custom](bool enabled) mutable { custom = enabled; }).Key("custom-toggle"),
        Checkbox("实时预览", live.Get()).OnChanged([=](bool enabled) mutable {
          live = enabled;
          if (enabled) RequestPreview(session.Get(), tasks, editor.Get().text, document, stats, message);
          else { session.Get()->CancelPending(); message = "实时预览已暂停，点击渲染手动更新。"; }
        }).Key("live-toggle"),
      }.With(Spacing(8.0F)),
      std::move(body).With(Grow{}),
      Text(message).With(Foreground{theme.colors.on_surface_variant}).Key("status"),
      Text(DescribeParse(stats.Get())).With(Foreground{theme.colors.on_surface_variant}).Key("parse-stats"),
    }.With(Padding(compact ? 12.0F : 24.0F), Spacing(12.0F), CrossAlign(CrossAxisAlignment::Stretch),
           Background{theme.colors.background});
  }));
}

} // namespace markdown_preview

const huxerui::Application application{
  markdown_preview::App,
  {.window = {.title = "HuxerUI Markdown Playground", .initial_size = {1180.0F, 820.0F}}, .show_debug_overlay = false},
};
