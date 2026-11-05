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

No release tag has been created. Syntax support and rendering defaults are unchanged.
