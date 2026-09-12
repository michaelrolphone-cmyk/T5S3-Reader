# Markdown support

## Current behavior

`.md` files are treated as plain-text documents.

- The file browser lists them next to `.txt`.
- Opening a `.md` file uses the existing TXT reader (`Txt` + `TxtReaderActivity`).
- Progress, recent books, covers next to the file (`notes.bmp` / `notes.jpg`), and sleep-cover lookup work the same as TXT.
- WebDAV serves `.md` as `text/markdown`.

`FsHelpers::classifyDocument()` returns `DocumentKind::Markdown` for `.md` and `DocumentKind::Txt` for `.txt`. `FsHelpers::isPlainTextReadable()` is true for both.

## Next step — markup rendering

Do not special-case `".md"` in new UI code. Branch on `DocumentKind::Markdown`.

A later renderer can:

1. Keep `Txt` as the byte loader (`readContent`).
2. Add a Markdown tokenizer / block parser in front of the existing page-layout path.
3. Introduce `MarkdownReaderActivity` (or a layout mode on `TxtReaderActivity`) without changing file-browser filters.

Out of scope for this step: headings, lists, emphasis, links, fenced code, images inside the document.
