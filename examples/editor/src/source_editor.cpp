#include "source_editor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace markdown_writer {
namespace {

using huxerui::TextEditingValue;
using huxerui::TextOffset;
using huxerui::TextRange;

constexpr std::size_t kTargetChunkBytes = 16 * 1024;

struct SourceChunk {
  std::uint64_t id = 0;
  std::string text;
  std::size_t byte_offset = 0;
  TextOffset utf16_offset = 0;
  TextOffset utf16_length = 0;
};

struct SourceEditorModel {
  TextEditingValue value;
  std::vector<SourceChunk> chunks;
  std::uint64_t next_id = 1;
  std::uint64_t active_id = 0;
};

void ValidateValue(const TextEditingValue& value) {
  const huxerui::AttributedText text(value.text);
  (void)text.TextInRange({0, value.selection.anchor});
  (void)text.TextInRange({0, value.selection.active});
  if (value.composition) (void)text.TextInRange(*value.composition);
  if (value.selection.affinity != huxerui::TextAffinity::Upstream &&
      value.selection.affinity != huxerui::TextAffinity::Downstream) {
    throw std::invalid_argument("Invalid large source editor selection affinity");
  }
}

std::size_t ChunkEnd(const std::string& source, std::size_t begin) {
  if (source.size() - begin <= kTargetChunkBytes) return source.size();

  const std::size_t target_end = begin + kTargetChunkBytes;
  const std::size_t previous_newline = source.rfind('\n', target_end);
  if (previous_newline != std::string::npos && previous_newline >= begin) {
    return previous_newline + 1;
  }
  const std::size_t next_newline = source.find('\n', target_end);
  if (next_newline != std::string::npos) return next_newline + 1;

  // An unterminated long line still needs bounded native layouts. Never split
  // a UTF-8 sequence when falling back to a byte-oriented boundary.
  std::size_t end = target_end;
  while (end > begin && (static_cast<unsigned char>(source[end]) & 0xC0U) == 0x80U) --end;
  return end > begin ? end : target_end + 1;
}

TextOffset Utf16Length(std::string_view text) {
  TextOffset length = 0;
  for (const unsigned char byte : text) {
    if ((byte & 0xC0U) == 0x80U) continue;
    length += byte >= 0xF0U ? 2 : 1;
  }
  return length;
}

std::size_t ActiveChunkIndex(const SourceEditorModel& model);

void Rebuild(SourceEditorModel& model, const TextEditingValue& value) {
  model.value = value;
  model.chunks.clear();

  std::size_t begin = 0;
  TextOffset utf16_offset = 0;
  do {
    const std::size_t end = value.text.empty() ? 0 : ChunkEnd(value.text, begin);
    std::string text = value.text.substr(begin, end - begin);
    const TextOffset length = Utf16Length(text);
    model.chunks.push_back({
        .id = model.next_id++,
        .text = std::move(text),
        .byte_offset = begin,
        .utf16_offset = utf16_offset,
        .utf16_length = length,
    });
    begin = end;
    utf16_offset += length;
  } while (begin < value.text.size());
  model.active_id = model.chunks[ActiveChunkIndex(model)].id;
}

std::size_t ChunkIndex(const SourceEditorModel& model, std::uint64_t id) {
  const auto found = std::find_if(model.chunks.begin(), model.chunks.end(),
      [id](const SourceChunk& chunk) { return chunk.id == id; });
  return static_cast<std::size_t>(found - model.chunks.begin());
}

std::size_t ActiveChunkIndex(const SourceEditorModel& model) {
  std::size_t active = 0;
  const TextOffset offset = model.value.selection.active;
  for (std::size_t index = 1; index < model.chunks.size(); ++index) {
    if (model.chunks[index].utf16_offset > offset) break;
    active = index;
  }
  return active;
}

TextOffset ChunkLength(const SourceChunk& chunk) {
  return chunk.utf16_length;
}

TextOffset ClampToChunk(TextOffset offset, const SourceChunk& chunk) {
  const TextOffset length = ChunkLength(chunk);
  if (offset <= chunk.utf16_offset) return 0;
  if (offset >= chunk.utf16_offset + length) return length;
  return offset - chunk.utf16_offset;
}

TextEditingValue ChunkValue(const SourceEditorModel& model, std::size_t index, bool active) {
  const auto& chunk = model.chunks[index];
  TextEditingValue result = TextEditingValue::FromText(chunk.text);
  if (!active) {
    result.selection = {};
    return result;
  }

  result.selection = {
      .anchor = ClampToChunk(model.value.selection.anchor, chunk),
      .active = model.value.selection.active - chunk.utf16_offset,
      .affinity = model.value.selection.affinity,
  };
  if (model.value.composition) {
    const auto composition = *model.value.composition;
    const TextOffset begin = chunk.utf16_offset;
    const TextOffset end = begin + ChunkLength(chunk);
    if (composition.end >= begin && composition.start <= end) {
      result.composition = TextRange{
          ClampToChunk(composition.start, chunk),
          ClampToChunk(composition.end, chunk),
      };
    }
  }
  return result;
}

bool ApplyLocalChange(
    SourceEditorModel& model,
    std::uint64_t id,
    TextEditingValue next
) {
  const std::size_t index = ChunkIndex(model, id);
  if (index == model.chunks.size()) return false;
  auto& chunk = model.chunks[index];

  const std::size_t old_bytes = chunk.text.size();
  const TextOffset old_utf16 = ChunkLength(chunk);
  const TextOffset new_utf16 = Utf16Length(next.text);
  const TextOffset chunk_utf16_offset = chunk.utf16_offset;
  model.value.text.replace(chunk.byte_offset, old_bytes, next.text);

  const std::size_t new_bytes = next.text.size();
  chunk.text = std::move(next.text);
  chunk.utf16_length = new_utf16;
  for (std::size_t later = index + 1; later < model.chunks.size(); ++later) {
    if (new_bytes >= old_bytes) model.chunks[later].byte_offset += new_bytes - old_bytes;
    else model.chunks[later].byte_offset -= old_bytes - new_bytes;
    model.chunks[later].utf16_offset += new_utf16 - old_utf16;
  }

  model.value.selection = {
      .anchor = chunk_utf16_offset + next.selection.anchor,
      .active = chunk_utf16_offset + next.selection.active,
      .affinity = next.selection.affinity,
  };
  if (next.composition) {
    model.value.composition = TextRange{
        chunk_utf16_offset + next.composition->start,
        chunk_utf16_offset + next.composition->end,
    };
  } else {
    model.value.composition.reset();
  }
  model.active_id = id;
  return true;
}

huxerui::TextFieldStyle ChunkFieldStyle(
    const huxerui::ThemeSpec& theme,
    const huxerui::TextStyle& text_style
) {
  auto style = huxerui::detail::DefaultTextFieldStyle(theme);
  style.variant = huxerui::TextFieldVariant::Standard;
  style.show_label = false;
  style.text_style = text_style;
  style.standard.minimum_height = 0.0F;
  style.border_width = 0.0F;
  style.focused_border_width = 0.0F;
  style.validation_border_width = 0.0F;
  style.focused_validation_border_width = 0.0F;
  style.padding = {};
  return style;
}

} // namespace

huxerui::View LargeSourceEditor(TextEditingValue value, LargeSourceEditorOptions options) {
  return huxerui::Scope([value = std::move(value), options = std::move(options)]() -> huxerui::View {
    using namespace huxerui;
    auto holder = UseState(std::shared_ptr<SourceEditorModel>{});
    if (!holder.Get()) holder = std::make_shared<SourceEditorModel>();
    const auto model = holder.Get();
    auto revision = UseState(std::uint64_t{0});
    (void)revision.Get();

    if (model->chunks.empty() || model->value.text != value.text) {
      ValidateValue(value);
      Rebuild(*model, value);
    } else if (model->value != value) {
      // Text unchanged: this is an external selection/composition update, not
      // a reason to replace chunk identities or native text layouts.
      ValidateValue(value);
      model->value = value;
      model->active_id = model->chunks[ActiveChunkIndex(*model)].id;
    }

    const auto changed = options.changed;
    auto list = VirtualList(model->chunks.size(), [model, revision, changed](std::size_t index) -> View {
      const auto& chunk = model->chunks[index];
      const std::uint64_t id = chunk.id;
      return Scope([model, index, id, revision, changed]() -> View {
        (void)revision.Get();
        return TextField(ChunkValue(*model, index, id == model->active_id))
            .Variant(TextFieldVariant::Standard)
            .LineLimits(TextFieldLineLimits::MultiLine(1))
            .VerticalAlign(TextVerticalAlign::Top)
            .OnChanged([model, id, revision, changed](const TextEditingValue& next) mutable {
              if (!ApplyLocalChange(*model, id, next)) return;
              revision = revision.Get() + 1;
              if (changed) changed(model->value);
            })
            .Key("writer-source-chunk-" + std::to_string(id));
      }).Key(id);
    }).EstimatedItemExtent(256.0F).CacheExtent(128.0F).Key("writer-source-list");

    ThemeDefinition field_theme;
    field_theme.Set(ChunkFieldStyle(UseTheme(), options.style));
    return Theme(std::move(field_theme), std::move(list).With(ScrollBar{}));
  });
}

} // namespace markdown_writer
