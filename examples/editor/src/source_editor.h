#pragma once

#include <functional>

#include <huxerui/huxerui.h>

namespace markdown_writer {

struct LargeSourceEditorOptions {
  huxerui::TextStyle style;
  std::function<void(const huxerui::TextEditingValue&)> changed;
};

[[nodiscard]] huxerui::View LargeSourceEditor(
    huxerui::TextEditingValue value,
    LargeSourceEditorOptions options
);

} // namespace markdown_writer
