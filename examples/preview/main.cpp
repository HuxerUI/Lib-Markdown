#include <huxerui/huxerui.h>
#include <huxerui/markdown.h>

using namespace huxerui;
using namespace huxerui::markdown;

View App() {
  auto dark = UseState(false);
  auto target = UseState(std::string("Click a link to inspect its destination."));
  static const auto document = ParseMarkdown(R"md(# HuxerUI Markdown

原生 Markdown 组件，支持 **粗体**、*斜体*、~~删除线~~ 和 `inline code`。

## Native text, native layout

One paragraph wraps naturally with **nested *emphasis*** and [relative links](../README.md).
Visit [HuxerUI](https://github.com/HuxerUI/HuxerUI) for the framework.

> Markdown is rendered with native HuxerUI views.
>
> The surrounding theme controls typography and colors.

### Lists and tasks

3. Start with an ordered list
4. Nest other content
   - A bullet with **bold text**
   - Unicode works: 中文 / 日本語 / 😀

- [x] CommonMark parser
- [x] GFM tables and task lists
- [ ] Your next extension

### Fenced code

```cpp
#include <huxerui/markdown.h>

View Article() {
  return Markdown("# Hello\n\n**HuxerUI**");
}
```

### Tables

| Feature | Status | Notes |
| :--- | :---: | ---: |
| Text styles | Ready | UTF-8 / UTF-16 |
| Links | Ready | App-owned navigation |
| Images | Custom | Supply an image renderer |

---

![Images use alt text by default](preview.png)

HTML stays literal: <strong>safe text</strong>.
)md");
  return MaterialTheme(dark.Get() ? MaterialDarkThemeSpec() : MaterialLightThemeSpec(), Scope([=] {
    return Column{
      Row{
        Text("Lib-Markdown", TextRole::Title).With(Grow{}),
        Button(dark.Get() ? "Light theme" : "Dark theme").OnClick([dark]() mutable { dark = !dark.Get(); }),
      }.With(Spacing(12.0F)),
      Text(target),
      ScrollView(Markdown(document, {.on_link = [target](const std::string& link) mutable { target = link; }}))
          .With(Grow{}, ScrollBar{}),
    }.With(Padding(24.0F), Spacing(16.0F), CrossAlign(CrossAxisAlignment::Stretch), Background{UseTheme().colors.background});
  }));
}

const Application application{App, {.window = {.title = "HuxerUI Markdown", .initial_size = {900.0F, 800.0F}}}};

int main() { return RunApplication(); }
