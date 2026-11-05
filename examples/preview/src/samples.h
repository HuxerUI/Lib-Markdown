#pragma once

#include <array>
#include <string>
#include <huxerui/markdown/document.h>

namespace markdown_preview {

struct Sample {
  std::string name;
  std::string source;
};

inline huxerui::markdown::ParseOptions DemoParseOptions() {
  return {.highlight = true, .insertions = true, .underline = true,
          .wiki_links = true, .admonitions = true, .footnotes = true,
          .math = true, .preserve_blank_lines = true, .spoilers = true};
}

inline const std::array<Sample, 7>& Samples() {
  static const std::array<Sample, 7> samples{{
    {"基础排版", R"md(# 你好，Markdown

用原生 HuxerUI 组件阅读和展示 Markdown。

## 一段文字，多种表达

这里有 **粗体**、*斜体*、***嵌套强调***、~~删除线~~ 和 `inline code`。
中文、English、日本語和 emoji 😀 都可以自然混排。

> 好的阅读体验，从清晰的文字开始。
>
> 试试切换深色主题，或选中文字复制。

### 换行与转义

普通换行会合并为一段。
这一行接在上一行后面。

行尾反斜杠产生硬换行。\
现在是新的一行。

\*这不是斜体\*，实体也会解码：&copy; &amp; &#x1F600;

---

从上方选择其他样例，或修改源码体验自动预览。关闭实时预览后可点击「渲染」。
)md"},
    {"列表与表格", R"md(# 列表与表格

## 项目清单

3. 使用自定义起始编号
4. 在列表中嵌套内容
   - 原生文本与布局
   - **强调**重点信息
     - 更深一层的列表

- [x] 完成 Markdown 解析
- [x] 支持明暗主题
- [ ] 创建你的下一篇文档

## 版本计划

| 功能 | 状态 | 优先级 |
| :--- | :---: | ---: |
| **富文本** | 已支持 | 1 |
| 表格横向滚动 | 已支持 | 2 |
| 自定义渲染 | 可接入 | 3 |

缩小窗口，试试横向滚动表格。任务复选框用于只读展示。

> 引用中也可以包含列表：
>
> - 第一项
> - 第二项
)md"},
    {"链接与边界", R"md(# 链接与边界

点击 [HuxerUI](https://github.com/HuxerUI/HuxerUI)，底部会显示回调收到的地址。

相对路径：[阅读说明](../README.md) · [中文路径](../文档/说明.md) · [页内锚点](#details)

相邻链接：[第一个](https://example.com/one)[第二个](https://example.com/two)

引用链接也可以使用：[组织主页][org]。

[org]: https://github.com/HuxerUI "HuxerUI organization"

## 安全边界

这个 [脚本地址](javascript:alert%281%29) 会保留文字，但不会成为可点击链接。

HTML 标签按文字展示：<strong>这里不会执行 HTML</strong>。

未接入图片加载器时显示替代文字：

![示例图片的替代文字](https://example.com/image.png)

## details

demo 只显示链接回调，不会打开浏览器或发起网络请求。
)md"},
    {"自定义渲染", R"md(# 自定义渲染

此样例打开自定义渲染开关：代码块显示语言与行号，独占段落图片交给应用绘制。

```cpp
#include <huxerui/markdown.h>

View Article() {
  return Markdown("# Hello\n\n**HuxerUI**");
}
```

## 本地示意图

![山与日落](demo:landscape "本地绘制，无需下载")

这个 `demo:landscape` 地址由 demo 自己识别，展示本地 Canvas 画面。
切换「自定义渲染」可以对比组件默认的代码块和图片替代文字。
)md"},
    {"长文档", [] {
      std::string text = "# 长文档阅读\n\n20 个章节，用于体验滚动与增量更新。编辑末尾内容，观察底部重解析字节数和复用块数。\n\n";
      for (int i = 1; i <= 20; ++i) {
        text += "## 第 " + std::to_string(i) + " 节\n\n";
        text += "这是一段包含 **重点**、*提示* 和 `code` 的说明。中文和 English 混排，"
                "段落会随着可用宽度自动换行。\n\n";
        text += "- 保留预解析文档，切换主题时复用\n- 选中文字并复制\n\n";
      }
      return text;
    }()},
    {"MD4C 扩展", R"md(# MD4C 原生扩展

==高亮文字==、++新增文字++、_下划线_，以及 **==嵌套格式==**。

Wiki 链接：[[文档/开始|项目文档]]，目标交给应用处理。

剧透：||点击揭示 **隐藏内容** 与 [链接](../spoiler.md)||，每个剧透单独揭示。

> [!NOTE]
> 提示内容支持 **粗体** 和 ==高亮==。

> [!TIP]
> 切换虚拟化，比较长文档的按需挂载。

> [!IMPORTANT]
> 扩展通过 ParseOptions 显式开启。

> [!WARNING]
> 启用下划线后，请使用星号输入斜体。

> [!CAUTION]
> Wiki 链接不会自动转换为网址或打开浏览器。

脚注示例[^说明]，同一脚注再次引用[^说明]，另一个脚注[^第二]。

[^说明]: 脚注保留中文标签，按首次引用顺序编号。
[^第二]: 后追加的定义可能影响前文，自动回退全量解析。
)md"},
    {"公式与空行", R"md(# 公式语法与空行

混排中的 $x^2 + y^2$ 以等宽 LaTeX 源码显示，尚未接入行内公式排版。

$$\frac{a}{b}$$

上方独占公式进入自定义渲染接口。此卡片仅展示源码，不是数学排版引擎。


这里之前有两个空行，保留为显式空白块，可同时测试虚拟化与复制。

`$代码中的内容保持字面$`，未闭合的 $标记也保持字面。
)md"},
  }};
  return samples;
}

} // namespace markdown_preview
