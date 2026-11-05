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
- Enable cancellable 120 ms live preview by default, with manual mode, error
  retention, parse diagnostics and preserved scroll during editing.
- Add differential parser edits and virtual-time live-preview interaction tests.

No release tag has been created. Syntax support and rendering defaults are unchanged.
