# MD4C

Vendored from https://github.com/mity/md4c at commit
`f9e5eb232b95b737373ae706648cb75bd259ed1a`.

Lib-Markdown carries an internal `block_source` observer in `md4c.c` and
`md4c.h`. It records container opener lines, leaf and table cell bounds, and
Setext and code fence tails for source editing. Keep the source-range and
incremental parser regressions when updating the vendored revision. The parser
and HTML entity lookup are MIT licensed.
