#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace huxerui::markdown {

/// CommonMark/GFM syntax kinds; Code is inline code, CodeBlock is block content.
enum class NodeKind {
  Document, Paragraph, Heading, BlockQuote, UnorderedList, OrderedList, ListItem,
  ThematicBreak, CodeBlock, Table, TableHead, TableBody, TableRow, TableHeaderCell,
  TableCell, Admonition, FootnoteSection, FootnoteDefinition, BlankLines,
  Emphasis, Strong, Strikethrough, Underline, Highlight, Insertion, Code, Link,
  WikiLink, FootnoteReference, InlineMath, DisplayMath, Spoiler, Image, Text,
};

enum class AdmonitionKind { Note, Tip, Important, Warning, Caution };

/// Source alignment for a table cell. Default uses leading paragraph alignment.
enum class CellAlignment { Default, Left, Center, Right };

/// Owned UTF-8 syntax tree. No node borrows the input or MD4C callback storage.
struct MarkdownNode {
  NodeKind kind = NodeKind::Text;
  /// UTF-8 literal for Text, Code, CodeBlock and math bodies (without $ delimiters).
  std::string text;
  std::vector<MarkdownNode> children;
  /// 1..6 for Heading; zero otherwise.
  unsigned heading_level = 0;
  /// First ordinal of OrderedList; default 1.
  unsigned list_start = 1;
  /// Whether list items omit blank paragraph separation.
  bool tight = false;
  /// Only task ListItem nodes have a value; false means unchecked.
  std::optional<bool> task_checked;
  /// Entity-decoded link/image destination or opaque WikiLink target. Never fetched/resolved.
  std::string destination;
  std::string title;
  /// Complete decoded code-fence info string; empty for indented code or other nodes.
  std::string fence_info;
  /// First word of fence_info; empty when no language was supplied.
  std::string language;
  CellAlignment alignment = CellAlignment::Default;
  /// Only meaningful on Admonition.
  AdmonitionKind admonition_kind = AdmonitionKind::Note;
  /// FootnoteReference/FootnoteDefinition: assigned by first-reference order, starting at 1.
  unsigned footnote_number = 0;
  /// Entity-decoded label; not a source coordinate or globally stable identity.
  std::string footnote_label;
  /// FootnoteReference: ordinal among references to this definition, starting at 1.
  unsigned footnote_reference = 0;
  /// FootnoteDefinition: total references in this parse.
  unsigned footnote_reference_count = 0;
  /// BlankLines: count reported by MD4C; not a source position or byte offset.
  unsigned blank_line_count = 0;

  bool operator==(const MarkdownNode&) const = default;
};

/// Controls display of an ordinary source line break; explicit hard breaks are unchanged.
enum class SoftBreakMode {
  /// Merge an ordinary source newline into one space (default).
  Space,
  /// Preserve an ordinary source newline as a displayed line break.
  LineBreak,
};

/// Bare-text autolinks only; explicit Markdown links and <angle autolinks> are
/// parsed independently. Defaults preserve the library's previous behavior.
struct MarkdownAutolinkOptions {
  bool urls = true;
  bool emails = true;
  bool www = true;
  bool operator==(const MarkdownAutolinkOptions&) const = default;
};

/// Synchronous parse configuration. No UI configuration or thread scheduling.
struct ParseOptions {
  bool tables = true;
  bool strikethrough = true;
  bool task_lists = true;
  MarkdownAutolinkOptions autolinks;
  /// Optional MD4C extensions, disabled by default. No third-party flags leak into this API.
  bool highlight = false; ///< ==highlight==
  bool insertions = false; ///< ++inserted++ (displayed with underline)
  /// Changes _ and __ to underline; use * and ** for emphasis when enabled.
  bool underline = false;
  bool wiki_links = false; ///< [[target]] or [[target|label]]; opaque application-owned target.
  bool admonitions = false; ///< > [!NOTE], TIP, IMPORTANT, WARNING, CAUTION.
  bool footnotes = false; ///< [^label] and [^label]: definition; can affect earlier content.
  /// Recognize $...$ / $$...$$. Stores literal LaTeX, does not validate/typeset it.
  bool math = false;
  /// Emit BlankLines nodes for runs reported by MD4C; not lossless source retention.
  bool preserve_blank_lines = false;
  bool spoilers = false; ///< ||hidden text||; visual concealment, not data protection.
  bool collapse_whitespace = false; ///< Collapse nontrivial ordinary whitespace; code remains literal.
  bool permissive_atx_headings = false; ///< Accept #heading without a separating space.
  bool indented_code_blocks = true; ///< False keeps fenced code but treats indentation as paragraph content.
  /// Does not change explicit hard breaks or newlines inside code blocks/spans.
  SoftBreakMode soft_break_mode = SoftBreakMode::Space;
  /// Positive UTF-8 input-byte limit, default 8 MiB; exceeding it throws length_error.
  std::size_t max_input_bytes = 8 * 1024 * 1024;
  /// Positive nesting limit, default 128, maximum 512; exceeded depth throws length_error.
  std::size_t max_nesting = 128;
  bool operator==(const ParseOptions&) const = default;
  /// Syntax presets; raw HTML remains literal in both. Individual fields remain
  /// editable. CommonMark disables optional GFM/bare autolinks; GitHub enables
  /// MD4C's GitHub dialect including admonitions and footnotes.
  [[nodiscard]] static ParseOptions CommonMark();
  [[nodiscard]] static ParseOptions GitHub();
};

/// Immutable, cheaply copied document snapshot. Copies share storage; equality compares
/// snapshot identity, not AST content. Safe to prepare on a worker and reuse on the UI thread.
class MarkdownDocument final {
public:
  /// Creates a valid empty document with a Document root.
  MarkdownDocument();
  MarkdownDocument(const MarkdownDocument&) = default;
  MarkdownDocument& operator=(const MarkdownDocument&) = default;
  /// Moving retains a valid source sharing the same snapshot; no tree is copied.
  MarkdownDocument(MarkdownDocument&& other) noexcept : storage_(other.storage_) {}
  MarkdownDocument& operator=(MarkdownDocument&& other) noexcept {
    storage_ = other.storage_;
    return *this;
  }

  /// Borrows the root while an owner of this snapshot remains alive.
  [[nodiscard]] const MarkdownNode& Root() const noexcept;
  /// Returns owned UTF-8 text without markup.
  [[nodiscard]] std::string PlainText() const;
  /// Snapshot identity equality; separately parsed equivalent ASTs need not be equal.
  bool operator==(const MarkdownDocument&) const noexcept = default;

private:
  struct Storage;
  explicit MarkdownDocument(std::shared_ptr<const Storage> storage) : storage_(std::move(storage)) {}
  std::shared_ptr<const Storage> storage_;
  friend MarkdownDocument ParseMarkdown(std::string_view, ParseOptions);
  friend class MarkdownParseSession;
};

/// Parse CommonMark plus the explicitly enabled extensions in ParseOptions.
/// HTML is literal text. Invalid UTF-8/options throw invalid_argument; resource limits
/// throw length_error. Parsing errors throw runtime_error; allocation errors propagate.
/// May run on a worker. Owns all resulting text; the source need only survive this call.
/// Empty source yields a valid empty document; no incremental state is retained.
[[nodiscard]] MarkdownDocument ParseMarkdown(std::string_view source, ParseOptions options = {});

/// How the latest successful session update produced its snapshot.
enum class MarkdownParseMode { Unchanged, Incremental, Full };

/// Primary document parse work for one update, not total CPU/memory work.
/// Source validation/comparison and copying retained AST blocks are separate from parsed_bytes.
struct MarkdownParseStats {
  MarkdownParseMode mode = MarkdownParseMode::Unchanged;
  std::size_t parsed_bytes = 0;
  std::size_t reused_blocks = 0;
  std::size_t total_blocks = 0;
  bool operator==(const MarkdownParseStats&) const = default;
};

/// One replacement in the session's current source, measured in UTF-8 bytes, not
/// HuxerUI's UTF-16 text offsets. Both boundaries must be at Unicode scalar boundaries.
struct MarkdownEdit {
  std::size_t start_byte = 0;
  std::size_t removed_bytes = 0;
  std::string inserted_text;
};

/// Stateful synchronous incremental parser with immutable output snapshots.
/// Reuses an unchanged prefix at parser-confirmed top-level restart boundaries and
/// reparses the tail. Potential reference definitions or an unavailable safe boundary
/// trigger full parsing. Results have the same semantics as ParseMarkdown.
/// Source scanning and assembling the owned AST remain linear; this is not a rope,
/// persistent-node tree, incremental renderer, or virtualized view.
/// Confine a session to one thread (or externally serialize access); retain/copy its
/// document snapshots independently. No UI or HuxerUI dependency is required.
class MarkdownParseSession final {
public:
  explicit MarkdownParseSession(ParseOptions options = {});
  ~MarkdownParseSession();
  MarkdownParseSession(const MarkdownParseSession&) = delete;
  MarkdownParseSession& operator=(const MarkdownParseSession&) = delete;

  /// Accepts complete UTF-8 source. Equal input preserves snapshot identity and parses
  /// zero bytes. Failure preserves Source(), Document() and LastParseStats().
  MarkdownDocument Update(std::string_view source);
  /// Applies one edit against the current source. Invalid byte ranges throw out_of_range;
  /// split scalars/invalid UTF-8 throw invalid_argument; size/depth limits throw length_error.
  MarkdownDocument ApplyEdit(const MarkdownEdit& edit);
  /// Appends complete UTF-8 text to the current source, using incremental parsing.
  /// For arbitrary byte chunks (including split scalars), use MarkdownStream.
  MarkdownDocument Append(std::string_view text);
  /// Borrowed UTF-8 source, invalidated by successful replacement or session destruction.
  [[nodiscard]] std::string_view Source() const noexcept;
  /// Borrowed current snapshot; copy it to retain it across updates/destruction.
  [[nodiscard]] const MarkdownDocument& Document() const noexcept;
  [[nodiscard]] MarkdownParseStats LastParseStats() const noexcept;

private:
  using ValidateDocument = std::function<void(const MarkdownDocument&)>;
  MarkdownDocument Update(std::string_view source, const ValidateDocument& validate);
  MarkdownDocument ApplyEdit(const MarkdownEdit& edit, const ValidateDocument& validate);
  struct Impl;
  explicit MarkdownParseSession(const Impl& snapshot);
  [[nodiscard]] MarkdownParseSession Fork() const;
  std::unique_ptr<Impl> impl_;
};

/// Synchronous incremental Markdown input accepting arbitrary UTF-8 byte chunks.
/// Buffers up to three trailing bytes of a split scalar; only complete valid UTF-8
/// enters the parser. Markdown structures may change as more input arrives (including
/// reference definitions affecting earlier links). No special partial-input grammar.
/// Thread-confined, noncopyable/nonmovable. Updates are transactional on failure.
class MarkdownStream final {
public:
  explicit MarkdownStream(ParseOptions options = {});
  ~MarkdownStream();
  MarkdownStream(const MarkdownStream&) = delete;
  MarkdownStream& operator=(const MarkdownStream&) = delete;

  /// Rejects invalid UTF-8 and input limits, including buffered bytes. Empty or purely
  /// incomplete chunks preserve the document/stats. Appending after Finish throws logic_error.
  MarkdownDocument Append(std::string_view bytes);
  /// Marks completion; throws invalid_argument for a truncated scalar without closing
  /// the stream, so its remaining bytes can still arrive. Idempotent after success.
  MarkdownDocument Finish();
  /// Borrowed committed UTF-8 source, excluding pending bytes. Invalidated on replacement.
  [[nodiscard]] std::string_view Source() const noexcept;
  [[nodiscard]] const MarkdownDocument& Document() const noexcept;
  [[nodiscard]] MarkdownParseStats LastParseStats() const noexcept;
  [[nodiscard]] std::size_t PendingByteCount() const noexcept;
  [[nodiscard]] bool IsFinished() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// Default interaction policy: http(s), mailto and relative targets are allowed.
/// Other explicit URI schemes, protocol-relative URLs and ASCII controls are rejected.
/// Parsing always preserves destinations; the renderer applies this policy to links.
/// This is an allow-policy predicate, not URI validation or a navigation safety guarantee.
[[nodiscard]] bool IsDefaultLinkAllowed(std::string_view target) noexcept;

/// Visible content of a node; inline markup is removed, images contribute alt text.
[[nodiscard]] std::string PlainText(const MarkdownNode& node);

} // namespace huxerui::markdown
