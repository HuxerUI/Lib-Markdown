#include <huxerui/markdown/document.h>

#include <charconv>
#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

#include "md4c.h"
extern "C" {
#include "entity.h"
}

namespace huxerui::markdown {
namespace {

void AppendCodepoint(std::string& out, std::uint32_t cp) {
  if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
  if (cp < 0x80) out += static_cast<char>(cp);
  else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 63));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 63));
    out += static_cast<char>(0x80 | (cp & 63));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 63));
    out += static_cast<char>(0x80 | ((cp >> 6) & 63));
    out += static_cast<char>(0x80 | (cp & 63));
  }
}

void ValidateUtf8(std::string_view input) {
  for (std::size_t i = 0; i < input.size();) {
    const auto lead = static_cast<unsigned char>(input[i++]);
    if (lead < 0x80) continue;
    unsigned remaining = 0;
    std::uint32_t cp = 0, minimum = 0;
    if (lead >= 0xC2 && lead <= 0xDF) { remaining = 1; cp = lead & 31; minimum = 0x80; }
    else if (lead >= 0xE0 && lead <= 0xEF) { remaining = 2; cp = lead & 15; minimum = 0x800; }
    else if (lead >= 0xF0 && lead <= 0xF4) { remaining = 3; cp = lead & 7; minimum = 0x10000; }
    else throw std::invalid_argument("Markdown source must be valid UTF-8");
    if (remaining > input.size() - i) throw std::invalid_argument("Truncated Markdown UTF-8");
    while (remaining--) {
      const auto c = static_cast<unsigned char>(input[i++]);
      if ((c & 0xC0) != 0x80) throw std::invalid_argument("Invalid Markdown UTF-8 continuation");
      cp = (cp << 6) | (c & 63);
    }
    if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
      throw std::invalid_argument("Invalid Markdown Unicode scalar");
  }
}

std::string DecodeEntity(std::string_view value) {
  std::string result;
  if (value.size() >= 4 && value[1] == '#') {
    auto digits = value.substr(2, value.size() - 3);
    int base = 10;
    if (!digits.empty() && (digits.front() == 'x' || digits.front() == 'X')) {
      digits.remove_prefix(1);
      base = 16;
    }
    std::uint32_t cp = 0;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), cp, base);
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) cp = 0xFFFD;
    AppendCodepoint(result, cp);
  } else if (const auto* entity = entity_lookup(value.data(), value.size())) {
    AppendCodepoint(result, entity->codepoints[0]);
    if (entity->codepoints[1]) AppendCodepoint(result, entity->codepoints[1]);
  } else result = value;
  return result;
}

std::string Attribute(const MD_ATTRIBUTE& attribute) {
  std::string result;
  for (unsigned i = 0; attribute.size && attribute.substr_offsets[i] < attribute.size; ++i) {
    const auto begin = attribute.substr_offsets[i];
    const auto size = attribute.substr_offsets[i + 1] - begin;
    const std::string_view part(attribute.text + begin, size);
    if (attribute.substr_types[i] == MD_TEXT_ENTITY) result += DecodeEntity(part);
    else if (attribute.substr_types[i] == MD_TEXT_NULLCHAR) result += "\xEF\xBF\xBD";
    else result += part;
  }
  return result;
}

struct Builder {
  MarkdownNode root{.kind = NodeKind::Document};
  std::vector<MarkdownNode*> stack{&root};
  ParseOptions options;
  std::exception_ptr error;

  template<class F> int Guard(F&& action) noexcept {
    try { action(); return 0; }
    catch (...) { error = std::current_exception(); return 1; }
  }

  MarkdownNode& Push(NodeKind kind) {
    if (stack.size() > options.max_nesting) throw std::length_error("Markdown nesting limit exceeded");
    auto& node = stack.back()->children.emplace_back();
    node.kind = kind;
    stack.push_back(&node);
    return node;
  }
};

int EnterBlock(MD_BLOCKTYPE type, void* detail, void* context) noexcept {
  auto& b = *static_cast<Builder*>(context);
  return b.Guard([&] {
    if (type == MD_BLOCK_DOC) return;
    NodeKind kind;
    switch (type) {
    case MD_BLOCK_QUOTE: kind = NodeKind::BlockQuote; break;
    case MD_BLOCK_UL: kind = NodeKind::UnorderedList; break;
    case MD_BLOCK_OL: kind = NodeKind::OrderedList; break;
    case MD_BLOCK_LI: kind = NodeKind::ListItem; break;
    case MD_BLOCK_HR: kind = NodeKind::ThematicBreak; break;
    case MD_BLOCK_H: kind = NodeKind::Heading; break;
    case MD_BLOCK_CODE: kind = NodeKind::CodeBlock; break;
    case MD_BLOCK_P: kind = NodeKind::Paragraph; break;
    case MD_BLOCK_TABLE: kind = NodeKind::Table; break;
    case MD_BLOCK_THEAD: kind = NodeKind::TableHead; break;
    case MD_BLOCK_TBODY: kind = NodeKind::TableBody; break;
    case MD_BLOCK_TR: kind = NodeKind::TableRow; break;
    case MD_BLOCK_TH: kind = NodeKind::TableHeaderCell; break;
    case MD_BLOCK_TD: kind = NodeKind::TableCell; break;
    default: throw std::runtime_error("Unexpected Markdown block");
    }
    auto& node = b.Push(kind);
    if (type == MD_BLOCK_H) node.heading_level = static_cast<MD_BLOCK_H_DETAIL*>(detail)->level;
    if (type == MD_BLOCK_UL) node.tight = static_cast<MD_BLOCK_UL_DETAIL*>(detail)->is_tight != 0;
    if (type == MD_BLOCK_OL) {
      const auto& d = *static_cast<MD_BLOCK_OL_DETAIL*>(detail);
      node.list_start = d.start;
      node.tight = d.is_tight != 0;
    }
    if (type == MD_BLOCK_LI) {
      const auto& d = *static_cast<MD_BLOCK_LI_DETAIL*>(detail);
      if (d.is_task) node.task_checked = d.task_mark != ' ';
    }
    if (type == MD_BLOCK_CODE) {
      const auto& d = *static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
      node.fence_info = Attribute(d.info);
      node.language = Attribute(d.lang);
    }
    if (type == MD_BLOCK_TH || type == MD_BLOCK_TD) {
      switch (static_cast<MD_BLOCK_TD_DETAIL*>(detail)->align) {
      case MD_ALIGN_LEFT: node.alignment = CellAlignment::Left; break;
      case MD_ALIGN_CENTER: node.alignment = CellAlignment::Center; break;
      case MD_ALIGN_RIGHT: node.alignment = CellAlignment::Right; break;
      default: break;
      }
    }
  });
}

int LeaveBlock(MD_BLOCKTYPE type, void*, void* context) noexcept {
  auto& b = *static_cast<Builder*>(context);
  return b.Guard([&] {
    if (type == MD_BLOCK_DOC) return;
    if (type == MD_BLOCK_LI) {
      auto& children = b.stack.back()->children;
      std::vector<MarkdownNode> normalized;
      for (auto& child : children) {
        if (child.kind >= NodeKind::Emphasis) {
          if (normalized.empty() || normalized.back().kind != NodeKind::Paragraph)
            normalized.push_back(MarkdownNode{.kind = NodeKind::Paragraph});
          normalized.back().children.push_back(std::move(child));
        } else normalized.push_back(std::move(child));
      }
      children = std::move(normalized);
    }
    b.stack.pop_back();
  });
}

int EnterSpan(MD_SPANTYPE type, void* detail, void* context) noexcept {
  auto& b = *static_cast<Builder*>(context);
  return b.Guard([&] {
    NodeKind kind;
    switch (type) {
    case MD_SPAN_EM: kind = NodeKind::Emphasis; break;
    case MD_SPAN_STRONG: kind = NodeKind::Strong; break;
    case MD_SPAN_DEL: kind = NodeKind::Strikethrough; break;
    case MD_SPAN_CODE: kind = NodeKind::Code; break;
    case MD_SPAN_A: kind = NodeKind::Link; break;
    case MD_SPAN_IMG: kind = NodeKind::Image; break;
    default: throw std::runtime_error("Unexpected Markdown span");
    }
    auto& node = b.Push(kind);
    if (type == MD_SPAN_A) {
      const auto& d = *static_cast<MD_SPAN_A_DETAIL*>(detail);
      node.destination = Attribute(d.href);
      node.title = Attribute(d.title);
    }
    if (type == MD_SPAN_IMG) {
      const auto& d = *static_cast<MD_SPAN_IMG_DETAIL*>(detail);
      node.destination = Attribute(d.src);
      node.title = Attribute(d.title);
    }
  });
}

int LeaveSpan(MD_SPANTYPE, void*, void* context) noexcept {
  static_cast<Builder*>(context)->stack.pop_back();
  return 0;
}

int TextCallback(MD_TEXTTYPE type, const MD_CHAR* data, MD_SIZE size, void* context) noexcept {
  auto& b = *static_cast<Builder*>(context);
  return b.Guard([&] {
    std::string text;
    switch (type) {
    case MD_TEXT_ENTITY: text = DecodeEntity({data, size}); break;
    case MD_TEXT_NULLCHAR: text = "\xEF\xBF\xBD"; break;
    case MD_TEXT_BR: text = "\n"; break;
    case MD_TEXT_SOFTBR: text = b.options.soft_break_mode == SoftBreakMode::LineBreak ? "\n" : " "; break;
    default: text.assign(data, size); break;
    }
    auto& parent = *b.stack.back();
    if (parent.kind == NodeKind::CodeBlock || parent.kind == NodeKind::Code) parent.text += text;
    else if (!parent.children.empty() && parent.children.back().kind == NodeKind::Text)
      parent.children.back().text += text;
    else parent.children.push_back(MarkdownNode{.kind = NodeKind::Text, .text = std::move(text)});
  });
}

bool IsBlock(NodeKind kind) {
  return kind <= NodeKind::TableCell;
}

void CollectText(const MarkdownNode& node, std::string& out) {
  out += node.text;
  for (std::size_t i = 0; i < node.children.size(); ++i) {
    if (i && IsBlock(node.children[i].kind) && !out.empty() && out.back() != '\n') out += '\n';
    CollectText(node.children[i], out);
  }
}

} // namespace

struct MarkdownDocument::Storage {
  MarkdownNode root;
  explicit Storage(MarkdownNode value) : root(std::move(value)) {}
};

MarkdownDocument::MarkdownDocument() {
  static const auto empty = std::make_shared<const Storage>(MarkdownNode{.kind = NodeKind::Document});
  storage_ = empty;
}
MarkdownDocument::MarkdownDocument(MarkdownNode root) : storage_(std::make_shared<const Storage>(std::move(root))) {}
const MarkdownNode& MarkdownDocument::Root() const noexcept { return storage_->root; }
std::string MarkdownDocument::PlainText() const { return markdown::PlainText(Root()); }
std::string PlainText(const MarkdownNode& node) {
  std::string out;
  CollectText(node, out);
  return out;
}

MarkdownDocument ParseMarkdown(std::string_view source, ParseOptions options) {
  if (options.soft_break_mode != SoftBreakMode::Space && options.soft_break_mode != SoftBreakMode::LineBreak)
    throw std::invalid_argument("Invalid Markdown soft break mode");
  if (!options.max_input_bytes || !options.max_nesting || options.max_nesting > 512)
    throw std::invalid_argument("Markdown limits must be positive; max_nesting must be <= 512");
  if (source.size() > options.max_input_bytes || source.size() > std::numeric_limits<MD_SIZE>::max())
    throw std::length_error("Markdown input limit exceeded");
  ValidateUtf8(source);
  Builder builder;
  builder.options = options;
  MD_PARSER parser{};
  parser.flags = MD_FLAG_NOHTML;
  if (options.tables) parser.flags |= MD_FLAG_TABLES;
  if (options.strikethrough) parser.flags |= MD_FLAG_STRIKETHROUGH;
  if (options.task_lists) parser.flags |= MD_FLAG_TASKLISTS;
  if (options.autolinks) parser.flags |= MD_FLAG_PERMISSIVEAUTOLINKS;
  parser.enter_block = EnterBlock;
  parser.leave_block = LeaveBlock;
  parser.enter_span = EnterSpan;
  parser.leave_span = LeaveSpan;
  parser.text = TextCallback;
  const int result = md_parse(source.empty() ? "" : source.data(), static_cast<MD_SIZE>(source.size()), &parser, &builder);
  if (builder.error) std::rethrow_exception(builder.error);
  if (result != 0) throw std::runtime_error("MD4C could not parse Markdown");
  return MarkdownDocument(std::move(builder.root));
}

bool IsDefaultLinkAllowed(std::string_view target) noexcept {
  if (target.empty()) return false;
  for (const unsigned char c : target) if (c <= 0x20 || c == 0x7F || c == '\\') return false;
  if (target.starts_with("//")) return false;
  const auto end = target.find_first_of("/?#");
  const auto colon = target.find(':');
  if (colon == std::string_view::npos || (end != std::string_view::npos && end < colon)) return true;
  const auto scheme = target.substr(0, colon);
  const auto equal = [scheme](std::string_view expected) {
    if (scheme.size() != expected.size()) return false;
    for (std::size_t i = 0; i < scheme.size(); ++i) {
      auto c = static_cast<unsigned char>(scheme[i]);
      if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c + ('a' - 'A'));
      if (c != static_cast<unsigned char>(expected[i])) return false;
    }
    return true;
  };
  return equal("http") || equal("https") || equal("mailto");
}

} // namespace huxerui::markdown
