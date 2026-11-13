# Changelog

## Unreleased

- Track official HuxerUI main commit `8d2bcea1` (0.4.0 development line; no
  v0.4.0 tag yet). Parser and view tests pass against it unchanged.

## 0.3.0 (2026-11-09)

- Trim the library to Markdown parsing plus read-only rendering. Remove
  `MarkdownEditor`, `editing.h`, `MarkdownRichText`, `MarkdownBlockEditSession`,
  `ApplyMarkdownCommand` and document source-range APIs.
- Delete the Writer demo (`examples/editor`), the rich-text editor experiment,
  Android/Linux Writer packaging, and the Writer release workflow.
- Depend on official HuxerUI 0.4.0; the fork-only `TextField::TextStyles` and
  `TextParagraphLayout` requirements are gone.
- This is a source-breaking release; see `docs/migration-0.3.md`.

## 0.2.4 (2026-11-08)

- Prevent caret reveal from overriding deliberate scrolling in the document editor.
  Ignore stale reveals after the user changes the offset, stop returning offscreen cached
  caret geometry to the platform's automatic reveal path, and keep explicit keyboard navigation.
- Add a regression that scrolls away from a selected paragraph before using keyboard reveal again.

## 0.2.23 (2026-11-08)

- Move the document edit session, cross-block selection, IME transactions and undo/redo
  from the Writer experiment into the public `MarkdownEditor` implementation without
  changing its public signature. Keep block/source fallback for empty or unsupported projections.
- Share the same editor model, Markdown backing, source-coordinate mapping, renderers and
  virtualization between the public component and Writer. External source replacement imports
  one undoable edit; Writer retains independent undo/redo history for every tab.
- Review HuxerUI PR #153 and PR #154 as sufficient for the editor requirements; native CI
  temporarily combines their reviewed commits while upstream review and merge remain pending.
- Add public editor coverage for source selection/composition mapping, Enter/undo, custom code
  renderers, external scroll control and external replacement, plus Writer per-tab history tests.
- Align with current HuxerUI conventions: remove command-only `nodiscard` annotations, simplify
  `State` reads, clarify platform terminology, and centralize explicit result discarding with a
  small library-private helper while preserving validation and subscription behavior.
- Use the conflict-free functional TextStyles commit from PR #153 when composing the temporary
  SDK for CI and release packaging, avoiding a conflict in its later style-only follow-up.

## 0.2.2 (2026-11-06)

- Replace Writer's fixed tab close button with an in-tab control shown on hover,
  place New Tab after the final tab, and show the packaged HUI logo instead of
  title text.
- Remount the retained whole-document editor when the selected tab changes so an
  opened file immediately renders its own contents.
- Virtualize source editing for tabs opened at 64 KiB or more using newline-aligned
  chunks of about 16 KiB. Relayout only the active chunk while preserving complete
  source bytes, global UTF-16 selection/composition, saving and live preview.

## 0.2.1 (2026-11-06)

Writer file workflow, whole-document editing by default, and split release packaging.

- Add Open File and Save to the Writer toolbar: read Markdown or plain-text files
  through the system picker into a new tab, write back when the file is writable,
  and refuse to switch documents while IME composition is active.
- Refresh the source-mode preview immediately when an edit adds or removes a line
  instead of waiting for the coalesced live-preview deadline.
- Use whole-document layout editing by default in document-mode Writer: Enter
  creates and renders a new paragraph at once, while single-block editing remains
  an explicit mode.
- Rebuild each tab's document edit session when switching or restoring tabs, so
  reopened drafts match their source text and fall back to source mode when the
  document cannot be laid out.
- Publish separate ARM64 and x86_64 Android APKs, assert the packaged ABI, and add
  a validated Linux desktop entry and icon to the DEB/RPM packages.

## 0.2.0 (2026-11-06)

Public API normalization; rebuild the library and applications together. See
[migration-0.2.md](docs/migration-0.2.md) for the source migration.

- Return a cheap immutable MarkdownDocument value from ParseMarkdown, with valid
  empty construction and snapshot-identity equality.
- Separate ParseOptions from display-only MarkdownOptions.
- Use MarkdownStyle/style for a complete local appearance override.
- Replace the on_link option with MarkdownEvents::LinkActivated and an owned
  MarkdownLinkActivation payload.
- Clarify code_block_renderer, standalone_image_renderer and callback types.
- Introduce SoftBreakMode and clarify AST/link-policy terminology.
- Migrate the demo and compile public API examples; add event lifetime/isolation
  and document snapshot regressions.
- Add MarkdownParseSession with full-source updates, UTF-8 byte edits, immutable
  snapshots, safe prefix reuse/tail reparsing and diagnostic statistics. Fall back
  to full parsing for potential references or unavailable restart boundaries.
- Enable cancellable 120 ms coalesced live preview by default, with manual mode, error
  retention, parse diagnostics and preserved scroll during editing.
- Keep the preview updating during held-key input by retaining the pending deadline
  and rendering the latest source instead of restarting a trailing debounce.
- Add differential parser edits and virtual-time live-preview interaction tests.
- Add MarkdownParseSession::Append and MarkdownStream for arbitrary UTF-8 byte chunks,
  transactional validation, pending scalar buffering and explicit Finish.
- Add VirtualizedMarkdown using measured top-level blocks, reused prefix/suffix
  identities, optional scrolling controller and logical offscreen text selection/copy.
- Add demo streaming replay/stop and virtualization controls, with parser chunk-split,
  lazy mounting, copy, scrolling and cancellation regressions.

- Expose MD4C highlight, insertion, underline, Wiki link, admonition and footnote
  extensions as opt-in ParseOptions, with owned AST metadata and native rendering.
- Add WikiLinkActivated / FootnoteActivated events, themed highlight/admonition
  styles and a custom admonition renderer. Wiki targets are opaque, not URI links.
- Normalize footnote inline bodies into paragraphs and keep definition/numbering
  updates on the conservative full-parse path; cover streaming and virtual selection.
- Add the MD4C Extensions demo sample and a capability/next-step matrix. Extend
  NodeKind without preserving numeric ABI; rebuild and update exhaustive visitors.

- Add optional literal math AST nodes with standalone formula renderer integration,
  explicit source fallback and no implicit TeX backend.
- Add BlankLines/count parsing, configurable spacing and interior logical copy
  separators in ordinary and virtual views, with differential edit/stream tests.
- Add pure UTF-16-aware ApplyMarkdownCommand formatting operations and the separate
  experimental Writer demo with a full-source mode. Build/test both apps independently.
- Add opt-in verified top-level source ranges, MarkdownBlockEditSession and
  MarkdownEditor for virtualized paragraph/heading in-place source editing with
  synchronous local preview. Keep the active native client through IME and block
  splits, and preserve source outside the edited interval. Writer uses the editor.
  Unified cross-block undo remains future work.
- Add MarkdownRichText and direct attributed paragraph/heading editing with hidden
  delimiters, retained inline marks, UTF-16 display/source coordinates, composition
  cancellation and bounded recent formatting restoration for native text undo.
  Verify generated Markdown with MD4C, keep neighbouring source bytes unchanged,
  and restore accepted content when a native proposal cannot be serialized.
- Enable attributed editing in nested list items, quotes, admonitions, footnote
  bodies and explicit table cells. Preserve enclosing syntax and validate nested
  replacements before publishing the document. Offer source fallback for verified
  code blocks and complex containers. Collect original offsets through a local MD4C observer.
- Share inline style resolution and link appearance between the preview and Writer.
- Require HuxerUI TextField::TextStyles; publish the SDK change on its own feature
  branch and pin verified commit e29c3c2573dcd7db201e7523ff59803529875d52 in CI.
- Add opt-in Spoiler AST/native click-to-reveal spans, granular URL/email/WWW
  autolink options, CommonMark/GitHub syntax presets, permissive ATX headings,
  optional indented code and ordinary whitespace normalization. Keep vendored MD4C
  unchanged and handle its tab-collapse defect in the adapter.
- Document rich editing boundaries and nonblocking follow-up decisions.

Default syntax remains unchanged; new extensions are explicitly enabled. Demo
parsing enables the optional extensions.
