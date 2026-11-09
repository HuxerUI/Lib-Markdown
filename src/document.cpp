#include <huxerui/markdown/document.h>

#include <charconv>
#include <algorithm>
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

std::size_t ValidateUtf8(std::string_view input, bool allow_incomplete = false) {
  for (std::size_t i = 0; i < input.size();) {
    const auto start = i;
    const auto lead = static_cast<unsigned char>(input[i++]);
    if (lead < 0x80) continue;
    unsigned remaining = 0;
    std::uint32_t cp = 0, minimum = 0;
    if (lead >= 0xC2 && lead <= 0xDF) { remaining = 1; cp = lead & 31; minimum = 0x80; }
    else if (lead >= 0xE0 && lead <= 0xEF) { remaining = 2; cp = lead & 15; minimum = 0x800; }
    else if (lead >= 0xF0 && lead <= 0xF4) { remaining = 3; cp = lead & 7; minimum = 0x10000; }
    else throw std::invalid_argument("Markdown source must be valid UTF-8");
    for (unsigned part = 0; part < remaining; ++part) {
      if (i == input.size()) {
        if (allow_incomplete) return start;
        throw std::invalid_argument("Truncated Markdown UTF-8");
      }
      const auto c = static_cast<unsigned char>(input[i++]);
      if ((c & 0xC0) != 0x80) throw std::invalid_argument("Invalid Markdown UTF-8 continuation");
      if (part == 0 && ((lead == 0xE0 && c < 0xA0) || (lead == 0xED && c >= 0xA0) ||
                        (lead == 0xF0 && c < 0x90) || (lead == 0xF4 && c >= 0x90)))
        throw std::invalid_argument("Invalid Markdown Unicode scalar");
      cp = (cp << 6) | (c & 63);
    }
    if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
      throw std::invalid_argument("Invalid Markdown Unicode scalar");
  }
  return input.size();
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
  struct Restart {
    std::size_t byte;
    std::size_t line_end;
    std::size_t block;
  };
  MarkdownNode root{.kind = NodeKind::Document};
  std::vector<MarkdownNode*> stack{&root};
  std::string_view source;
  std::vector<Restart> restarts;
  bool implicit_paragraph = false;
  bool first_content = true;
  bool collapsed_space = false;
  ParseOptions options;
  std::exception_ptr error;

  template<class F> int Guard(F&& action) noexcept {
    try { action(); return 0; }
    catch (...) { error = std::current_exception(); return 1; }
  }

  MarkdownNode& Push(NodeKind kind, bool implicit = false) {
    if (!implicit && stack.size() - implicit_paragraph > options.max_nesting)
      throw std::length_error("Markdown nesting limit exceeded");
    auto& node = stack.back()->children.emplace_back();
    node.kind = kind;
    stack.push_back(&node);
    return node;
  }

  void Pop() { stack.pop_back(); }

  void OpenImplicitParagraph() {
    const auto kind = stack.back()->kind;
    if (kind != NodeKind::ListItem && kind != NodeKind::FootnoteDefinition) return;
    Push(NodeKind::Paragraph, true);
    implicit_paragraph = true;
  }

  void CloseImplicitParagraph() {
    if (!implicit_paragraph) return;
    Pop();
    implicit_paragraph = false;
  }

  void RecordRestart(MD_TEXTTYPE type, const char* text) {
    if (stack.size() < 2 || !first_content) return;
    const auto kind = stack[1]->kind;
    if (kind != NodeKind::Heading && kind != NodeKind::Paragraph) return;
    const auto base = reinterpret_cast<std::uintptr_t>(source.data());
    const auto pointer = reinterpret_cast<std::uintptr_t>(text);
    if (source.empty() || pointer < base || pointer - base >= source.size()) {
      first_content = false;
      return;
    }
    first_content = false;
    std::size_t begin = pointer - base;
    while (begin && source[begin - 1] != '\n' && source[begin - 1] != '\r') --begin;
    auto end = source.find_first_of("\r\n", begin);
    if (end == std::string_view::npos) return; // An EOF line can still be extended.
    const auto line = source.substr(begin, end - begin);
    bool safe = false;
    if (kind == NodeKind::Heading) {
      std::size_t marker = 0;
      while (marker < line.size() && marker < 3 && line[marker] == ' ') ++marker;
      const auto start = marker;
      while (marker < line.size() && line[marker] == '#') ++marker;
      safe = marker > start && marker - start <= 6 &&
          (marker == line.size() || line[marker] == ' ' || line[marker] == '\t');
    } else if (type == MD_TEXT_NORMAL && stack.size() == 2 && begin > 0 && !line.starts_with("\xEF\xBB\xBF")) {
      // A plain first line of a root paragraph, preceded by an unchanged blank line,
      // establishes the same container closure in a suffix parse. Spans/breaks seen
      // before raw text disqualify the paragraph (its actual first line may be earlier).
      auto previous_end = begin;
      if (previous_end && source[previous_end - 1] == '\n') --previous_end;
      if (previous_end && source[previous_end - 1] == '\r') --previous_end;
      auto previous_begin = previous_end;
      while (previous_begin && source[previous_begin - 1] != '\n' && source[previous_begin - 1] != '\r') --previous_begin;
      safe = std::all_of(source.begin() + previous_begin, source.begin() + previous_end,
                         [](char c) { return c == ' ' || c == '\t'; });
    }
    if (source[end] == '\r' && end + 1 < source.size() && source[end + 1] == '\n') ++end;
    if (safe) restarts.push_back({begin, end + 1, root.children.size() - 1});
  }
};

int EnterBlock(MD_BLOCKTYPE type, void* detail, void* context) noexcept {
  auto& b = *static_cast<Builder*>(context);
  return b.Guard([&] {
    if (type == MD_BLOCK_DOC) return;
    b.CloseImplicitParagraph();
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
    case MD_BLOCK_ADMONITION: kind = NodeKind::Admonition; break;
    case MD_BLOCK_FOOTNOTE_DEF_SECTION: kind = NodeKind::FootnoteSection; break;
    case MD_BLOCK_FOOTNOTE_DEF: kind = NodeKind::FootnoteDefinition; break;
    case MD_BLOCK_BLANK: kind = NodeKind::BlankLines; break;
    default: throw std::runtime_error("Unexpected Markdown block");
    }
    auto& node = b.Push(kind);
    b.collapsed_space = false;
    if (b.stack.size() == 2) b.first_content = true;
    if (type == MD_BLOCK_BLANK)
      node.blank_line_count = static_cast<MD_BLOCK_BLANK_DETAIL*>(detail)->line_count;
    if (type == MD_BLOCK_ADMONITION) {
      const auto name = Attribute(static_cast<MD_BLOCK_ADMONITION_DETAIL*>(detail)->type);
      if (name == "tip") node.admonition_kind = AdmonitionKind::Tip;
      else if (name == "important") node.admonition_kind = AdmonitionKind::Important;
      else if (name == "warning") node.admonition_kind = AdmonitionKind::Warning;
      else if (name == "caution") node.admonition_kind = AdmonitionKind::Caution;
    }
    if (type == MD_BLOCK_FOOTNOTE_DEF) {
      const auto& d = *static_cast<MD_BLOCK_FOOTNOTE_DEF_DETAIL*>(detail);
      node.footnote_number = d.id;
      node.footnote_label = Attribute(d.label);
      node.footnote_reference_count = d.ref_count;
    }
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
    b.CloseImplicitParagraph();
    b.Pop();
  });
}

int EnterSpan(MD_SPANTYPE type, void* detail, void* context) noexcept {
  auto& b = *static_cast<Builder*>(context);
  return b.Guard([&] {
    // Paragraphs starting with a span might produce their first raw text on a later
    // line. ATX headings are single-line and remain eligible through inline spans.
    if (b.stack.size() >= 2 && b.stack[1]->kind == NodeKind::Paragraph) b.first_content = false;
    b.OpenImplicitParagraph();
    NodeKind kind;
    switch (type) {
    case MD_SPAN_EM: kind = NodeKind::Emphasis; break;
    case MD_SPAN_STRONG: kind = NodeKind::Strong; break;
    case MD_SPAN_DEL: kind = NodeKind::Strikethrough; break;
    case MD_SPAN_CODE: kind = NodeKind::Code; break;
    case MD_SPAN_A: kind = NodeKind::Link; break;
    case MD_SPAN_IMG: kind = NodeKind::Image; break;
    case MD_SPAN_U: kind = NodeKind::Underline; break;
    case MD_SPAN_MARK: kind = NodeKind::Highlight; break;
    case MD_SPAN_INS: kind = NodeKind::Insertion; break;
    case MD_SPAN_WIKILINK: kind = NodeKind::WikiLink; break;
    case MD_SPAN_FOOTNOTE_REF: kind = NodeKind::FootnoteReference; break;
    case MD_SPAN_LATEXMATH: kind = NodeKind::InlineMath; break;
    case MD_SPAN_LATEXMATH_DISPLAY: kind = NodeKind::DisplayMath; break;
    case MD_SPAN_SPOILER: kind = NodeKind::Spoiler; break;
    default: throw std::runtime_error("Unexpected Markdown span");
    }
    auto& node = b.Push(kind);
    if (type == MD_SPAN_WIKILINK)
      node.destination = Attribute(static_cast<MD_SPAN_WIKILINK_DETAIL*>(detail)->target);
    if (type == MD_SPAN_FOOTNOTE_REF) {
      const auto& d = *static_cast<MD_SPAN_FOOTNOTE_REF_DETAIL*>(detail);
      node.footnote_number = d.id;
      node.footnote_label = Attribute(d.label);
      node.footnote_reference = d.ref_id;
      node.text = "[" + std::to_string(d.id) + "]";
    }
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
  static_cast<Builder*>(context)->Pop();
  return 0;
}

int TextCallback(MD_TEXTTYPE type, const MD_CHAR* data, MD_SIZE size, void* context) noexcept {
  auto& b = *static_cast<Builder*>(context);
  return b.Guard([&] {
    b.RecordRestart(type, data);
    b.OpenImplicitParagraph();
    std::string text;
    switch (type) {
    case MD_TEXT_ENTITY: text = DecodeEntity({data, size}); break;
    case MD_TEXT_NULLCHAR: text = "\xEF\xBF\xBD"; break;
    case MD_TEXT_BR: text = "\n"; break;
    case MD_TEXT_SOFTBR: text = b.options.soft_break_mode == SoftBreakMode::LineBreak ? "\n" : " "; break;
    default: text.assign(data, size); break;
    }
    // The vendored MD4C collapse flag drops tab-started whitespace marks. Collapse
    // ordinary text here instead, retaining code/math/entities and source offsets.
    if (b.options.collapse_whitespace && type == MD_TEXT_NORMAL) {
      std::string collapsed;
      for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
          if (!b.collapsed_space) collapsed += ' ';
          b.collapsed_space = true;
        } else { collapsed += c; b.collapsed_space = false; }
      }
      text = std::move(collapsed);
    } else b.collapsed_space = type == MD_TEXT_SOFTBR && text == " ";
    auto& parent = *b.stack.back();
    if (parent.kind == NodeKind::CodeBlock || parent.kind == NodeKind::Code ||
        parent.kind == NodeKind::InlineMath || parent.kind == NodeKind::DisplayMath) parent.text += text;
    else if (!parent.children.empty() && parent.children.back().kind == NodeKind::Text)
      parent.children.back().text += text;
    else parent.children.push_back(MarkdownNode{.kind = NodeKind::Text, .text = std::move(text)});
  });
}

bool IsBlock(NodeKind kind) {
  switch (kind) {
  case NodeKind::Document: case NodeKind::Paragraph: case NodeKind::Heading:
  case NodeKind::BlockQuote: case NodeKind::UnorderedList: case NodeKind::OrderedList:
  case NodeKind::ListItem: case NodeKind::ThematicBreak: case NodeKind::CodeBlock:
  case NodeKind::Table: case NodeKind::TableHead: case NodeKind::TableBody:
  case NodeKind::TableRow: case NodeKind::TableHeaderCell: case NodeKind::TableCell:
  case NodeKind::Admonition: case NodeKind::FootnoteSection: case NodeKind::FootnoteDefinition:
  case NodeKind::BlankLines:
    return true;
  default: return false;
  }
}

void CollectText(const MarkdownNode& node, std::string& out) {
  if (node.kind == NodeKind::BlankLines) {
    out.append(node.blank_line_count, '\n');
    return;
  }
  out += node.text;
  for (std::size_t i = 0; i < node.children.size(); ++i) {
    if (i && IsBlock(node.children[i].kind) && !out.empty() && out.back() != '\n') out += '\n';
    CollectText(node.children[i], out);
  }
}

void ValidateInput(std::string_view source, const ParseOptions& options) {
  if (options.soft_break_mode != SoftBreakMode::Space && options.soft_break_mode != SoftBreakMode::LineBreak)
    throw std::invalid_argument("Invalid Markdown soft break mode");
  if (!options.max_input_bytes || !options.max_nesting || options.max_nesting > 512)
    throw std::invalid_argument("Markdown limits must be positive; max_nesting must be <= 512");
  if (source.size() > options.max_input_bytes || source.size() > std::numeric_limits<MD_SIZE>::max())
    throw std::length_error("Markdown input limit exceeded");
  ValidateUtf8(source);
}

struct ParsedSource {
  MarkdownNode root;
  std::vector<Builder::Restart> restarts;
};

ParsedSource ParseValidatedSource(std::string_view source, const ParseOptions& options) {
  Builder builder;
  builder.source = source;
  builder.options = options;
  MD_PARSER parser{};
  parser.flags = MD_FLAG_NOHTML;
  if (options.tables) parser.flags |= MD_FLAG_TABLES;
  if (options.strikethrough) parser.flags |= MD_FLAG_STRIKETHROUGH;
  if (options.task_lists) parser.flags |= MD_FLAG_TASKLISTS;
  if (options.autolinks.urls) parser.flags |= MD_FLAG_PERMISSIVEURLAUTOLINKS;
  if (options.autolinks.emails) parser.flags |= MD_FLAG_PERMISSIVEEMAILAUTOLINKS;
  if (options.autolinks.www) parser.flags |= MD_FLAG_PERMISSIVEWWWAUTOLINKS;
  if (options.spoilers) parser.flags |= MD_FLAG_SPOILERS;
  if (options.permissive_atx_headings) parser.flags |= MD_FLAG_PERMISSIVEATXHEADERS;
  if (!options.indented_code_blocks) parser.flags |= MD_FLAG_NOINDENTEDCODEBLOCKS;
  if (options.highlight) parser.flags |= MD_FLAG_HIGHLIGHT;
  if (options.insertions) parser.flags |= MD_FLAG_INSERT;
  if (options.underline) parser.flags |= MD_FLAG_UNDERLINE;
  if (options.wiki_links) parser.flags |= MD_FLAG_WIKILINKS;
  if (options.admonitions) parser.flags |= MD_FLAG_ADMONITIONS;
  if (options.footnotes) parser.flags |= MD_FLAG_FOOTNOTES;
  if (options.math) parser.flags |= MD_FLAG_LATEXMATHSPANS;
  if (options.preserve_blank_lines) parser.flags |= MD_FLAG_PRESERVEBLANKLINES;
  parser.enter_block = EnterBlock;
  parser.leave_block = LeaveBlock;
  parser.enter_span = EnterSpan;
  parser.leave_span = LeaveSpan;
  parser.text = TextCallback;
  const int result = md_parse(source.empty() ? "" : source.data(), static_cast<MD_SIZE>(source.size()), &parser, &builder);
  if (builder.error) std::rethrow_exception(builder.error);
  if (result != 0) throw std::runtime_error("MD4C could not parse Markdown");
  return {std::move(builder.root), std::move(builder.restarts)};
}

} // namespace

struct MarkdownDocument::Storage {
  MarkdownNode root;
  explicit Storage(MarkdownNode value) : root(std::move(value)) {}
};

ParseOptions ParseOptions::CommonMark() {
  ParseOptions result;
  result.tables = result.strikethrough = result.task_lists = false;
  result.autolinks = {false, false, false};
  return result;
}
ParseOptions ParseOptions::GitHub() {
  ParseOptions result;
  result.admonitions = result.footnotes = true;
  return result;
}

MarkdownDocument::MarkdownDocument() {
  static const auto empty = std::make_shared<const Storage>(MarkdownNode{.kind = NodeKind::Document});
  storage_ = empty;
}
const MarkdownNode& MarkdownDocument::Root() const noexcept { return storage_->root; }
std::string MarkdownDocument::PlainText() const { return markdown::PlainText(Root()); }
std::string PlainText(const MarkdownNode& node) {
  std::string out;
  CollectText(node, out);
  return out;
}

MarkdownDocument ParseMarkdown(std::string_view source, ParseOptions options) {
  ValidateInput(source, options);
  auto parsed = ParseValidatedSource(source, options);
  return MarkdownDocument(std::make_shared<const MarkdownDocument::Storage>(std::move(parsed.root)));
}

struct MarkdownParseSession::Impl {
  ParseOptions options;
  std::string source;
  MarkdownDocument document;
  std::vector<Builder::Restart> restarts;
  MarkdownParseStats stats;
};

MarkdownParseSession::MarkdownParseSession(ParseOptions options) : impl_(std::make_unique<Impl>()) {
  ValidateInput({}, options);
  impl_->options = options;
}
MarkdownParseSession::MarkdownParseSession(const Impl& snapshot) : impl_(std::make_unique<Impl>(snapshot)) {}
MarkdownParseSession MarkdownParseSession::Fork() const { return MarkdownParseSession(*impl_); }
MarkdownParseSession::~MarkdownParseSession() = default;
std::string_view MarkdownParseSession::Source() const noexcept { return impl_->source; }
const MarkdownDocument& MarkdownParseSession::Document() const noexcept { return impl_->document; }
MarkdownParseStats MarkdownParseSession::LastParseStats() const noexcept { return impl_->stats; }

MarkdownDocument MarkdownParseSession::Update(std::string_view source) {
  return Update(source, {});
}

MarkdownDocument MarkdownParseSession::Update(std::string_view source, const ValidateDocument& validate) {
  ValidateInput(source, impl_->options);
  if (source == impl_->source) {
    const auto count = impl_->document.Root().children.size();
    impl_->stats = {MarkdownParseMode::Unchanged, 0, count, count};
    return impl_->document;
  }
  std::size_t common = 0;
  while (common < source.size() && common < impl_->source.size() && source[common] == impl_->source[common]) ++common;
  std::size_t restart_byte = 0, retained = 0;
  // Every supported reference definition has a literal closing label followed by
  // ':'. The conservative check includes escaped/code occurrences intentionally.
  // Definitions anywhere can resolve references in an otherwise unchanged prefix.
  if (source.find("]:") == std::string_view::npos && impl_->source.find("]:") == std::string::npos) {
    for (const auto& restart : impl_->restarts) {
      if (restart.line_end > common) break;
      if (restart.block > 0) { restart_byte = restart.byte; retained = restart.block; }
    }
  }
  std::string owned_source(source);
  auto parsed = ParseValidatedSource(std::string_view(owned_source).substr(restart_byte), impl_->options);
  std::vector<Builder::Restart> restarts;
  if (retained) {
    MarkdownNode combined{.kind = NodeKind::Document};
    combined.children.reserve(retained + parsed.root.children.size());
    const auto& old = impl_->document.Root().children;
    combined.children.insert(combined.children.end(), old.begin(), old.begin() + retained);
    for (auto& block : parsed.root.children) combined.children.push_back(std::move(block));
    parsed.root = std::move(combined);
    for (const auto& restart : impl_->restarts)
      if (restart.block < retained) restarts.push_back(restart);
  }
  for (auto restart : parsed.restarts) {
    restart.byte += restart_byte;
    restart.line_end += restart_byte;
    restart.block += retained;
    restarts.push_back(restart);
  }
  MarkdownDocument next(std::make_shared<const MarkdownDocument::Storage>(std::move(parsed.root)));
  if (validate) validate(next);
  const MarkdownParseStats stats{
    retained ? MarkdownParseMode::Incremental : MarkdownParseMode::Full,
    owned_source.size() - restart_byte, retained, next.Root().children.size(),
  };
  // Commit only after parsing, copying and all allocations succeed.
  impl_->source.swap(owned_source);
  impl_->restarts.swap(restarts);
  impl_->document = next;
  impl_->stats = stats;
  return next;
}

MarkdownDocument MarkdownParseSession::Append(std::string_view text) {
  return ApplyEdit({impl_->source.size(), 0, std::string(text)});
}

MarkdownDocument MarkdownParseSession::ApplyEdit(const MarkdownEdit& edit) {
  return ApplyEdit(edit, {});
}

MarkdownDocument MarkdownParseSession::ApplyEdit(const MarkdownEdit& edit, const ValidateDocument& validate) {
  const auto& source = impl_->source;
  if (edit.start_byte > source.size() || edit.removed_bytes > source.size() - edit.start_byte)
    throw std::out_of_range("Markdown edit is outside the source");
  const auto boundary = [&](std::size_t offset) {
    return offset == source.size() || (static_cast<unsigned char>(source[offset]) & 0xC0) != 0x80;
  };
  if (!boundary(edit.start_byte) || !boundary(edit.start_byte + edit.removed_bytes))
    throw std::invalid_argument("Markdown edit splits a UTF-8 scalar");
  ValidateUtf8(edit.inserted_text);
  const auto remaining = source.size() - edit.removed_bytes;
  const auto limit = std::min<std::size_t>(impl_->options.max_input_bytes, std::numeric_limits<MD_SIZE>::max());
  if (edit.inserted_text.size() > limit - remaining) throw std::length_error("Markdown input limit exceeded");
  auto next = source;
  next.replace(edit.start_byte, edit.removed_bytes, edit.inserted_text);
  return Update(next, validate);
}

struct MarkdownStream::Impl {
  explicit Impl(ParseOptions options) : parser(options), limit(std::min<std::size_t>(
      options.max_input_bytes, std::numeric_limits<MD_SIZE>::max())) {}
  MarkdownParseSession parser;
  std::size_t limit;
  std::string pending;
  bool finished = false;
};

MarkdownStream::MarkdownStream(ParseOptions options) : impl_(std::make_unique<Impl>(options)) {}
MarkdownStream::~MarkdownStream() = default;
std::string_view MarkdownStream::Source() const noexcept { return impl_->parser.Source(); }
const MarkdownDocument& MarkdownStream::Document() const noexcept { return impl_->parser.Document(); }
MarkdownParseStats MarkdownStream::LastParseStats() const noexcept { return impl_->parser.LastParseStats(); }
std::size_t MarkdownStream::PendingByteCount() const noexcept { return impl_->pending.size(); }
bool MarkdownStream::IsFinished() const noexcept { return impl_->finished; }

MarkdownDocument MarkdownStream::Append(std::string_view bytes) {
  if (impl_->finished) throw std::logic_error("Markdown stream is finished");
  if (bytes.size() > impl_->limit - Source().size() - impl_->pending.size())
    throw std::length_error("Markdown input limit exceeded");
  auto combined = impl_->pending;
  combined.append(bytes);
  const auto complete = ValidateUtf8(combined, true);
  auto pending = combined.substr(complete);
  auto next = complete ? impl_->parser.Append(std::string_view(combined).substr(0, complete)) : Document();
  impl_->pending.swap(pending);
  return next;
}

MarkdownDocument MarkdownStream::Finish() {
  if (!impl_->pending.empty()) throw std::invalid_argument("Truncated Markdown stream UTF-8");
  impl_->finished = true;
  return Document();
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
