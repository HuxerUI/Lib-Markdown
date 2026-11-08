# Rich Text Editor prototype

The standalone example builds with HuxerUI and an attributed paragraph model. Each visible paragraph uses `Text(AttributedText)`; the virtual list has one document-level input and selection client. Paragraphs retain their logical IDs through selection, editing and scroll eviction.

The model and view now live in `src/rich_text_model.*` and `src/rich_text_editor.*`, shared with the public Markdown editor and Writer. This prototype builds those same sources without adding another implementation.

Use HuxerUI paragraph geometry from [PR #154](https://github.com/HuxerUI/HuxerUI/pull/154), reviewed at [`554d9a5`](https://github.com/HuxerUI/HuxerUI/commit/554d9a540b40fa5a7b51125218a80166c86d8679).

```powershell
cmake -S experiments/rich_text_editor -B build-rich-text -DHUXERUI_HOME=/path/to/HuxerUI -DHUXERUI_BUILD_TESTING_LIBRARY=ON
cmake --build build-rich-text --config Release
ctest --test-dir build-rich-text -C Release --output-on-failure
.\build-rich-text\Release\rich_text_editor_demo.exe
```

Keep the demo's adjacent resources directory when copying the executable. The model tests cover cross-paragraph transactions, Unicode boundaries, input method cancellation, clipboard text and history. The windowless tests exercise desktop mouse selection, a normalized Chinese composition commit, undo, and selection across scroll eviction. The demo targets desktop mouse and keyboard editing. Native candidate windows, touch gestures and platform accessibility are separate device validation.

Drag across the first two paragraphs, type a replacement, and use Ctrl+Z or Command+Z to restore the text and selection. Ctrl+B or Command+B toggles bold on the selected range.
