#pragma once

#include <array>
#include <string>

namespace markdown_preview {

struct Sample {
  std::string name;
  std::string source;
};

inline const std::array<Sample, 5>& Samples() {
  static const std::array<Sample, 5> samples{{
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

从上方选择其他样例，或在左侧修改源码后点击「渲染」。
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
      std::string text = "# 长文档阅读\n\n20 个章节，用于体验滚动、文本选择和文档切换。\n\n";
      for (int i = 1; i <= 20; ++i) {
        text += "## 第 " + std::to_string(i) + " 节\n\n";
        text += "这是一段包含 **重点**、*提示* 和 `code` 的说明。中文和 English 混排，"
                "段落会随着可用宽度自动换行。\n\n";
        text += "- 保留预解析文档，切换主题时复用\n- 选中文字并复制\n\n";
      }
      return text;
    }()},
  }};
  return samples;
}

} // namespace markdown_preview
