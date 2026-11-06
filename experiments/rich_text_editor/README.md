# Rich Text Editor prototype

The standalone example builds with HuxerUI and an attributed paragraph model. Each visible paragraph uses `Text(AttributedText)`. The virtual list has one document-level input and selection client. Paragraphs retain their logical IDs through selection, editing and scroll eviction.

Use the HuxerUI paragraph geometry SDK at [`e119529`](https://github.com/FarnaHerry/HuxerUI/commit/e119529d4fe118b0b4df4a0a306df6819814d7a0).

```powershell
cmake -S experiments/rich_text_editor -B build-rich-text -DHUXERUI_HOME=/path/to/HuxerUI -DHUXERUI_BUILD_TESTING_LIBRARY=ON
cmake --build build-rich-text --config Release
ctest --test-dir build-rich-text -C Release --output-on-failure
.\build-rich-text\Release\rich_text_editor_demo.exe
```

Keep the demo's adjacent resources directory when copying the executable. The model tests cover cross-paragraph transactions, Unicode boundaries, input method cancellation, clipboard text and history. The windowless tests exercise desktop mouse selection, a normalized Chinese composition commit, undo, and selection across scroll eviction. The demo targets desktop mouse and keyboard editing. Native candidate windows, touch gestures and platform accessibility are separate device validation.

Drag across the first two paragraphs, type a replacement, and use Ctrl+Z or Command+Z to restore the text and selection. Ctrl+B or Command+B toggles bold on the selected range.
