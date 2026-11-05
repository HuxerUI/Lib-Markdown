# Changelog

## 0.2.0 (unreleased)

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

No release tag has been created. Default syntax remains unchanged; new extensions
are explicitly enabled. Demo parsing enables the optional extensions.
