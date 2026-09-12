# Markdown support

## Current behavior

`.md` files open in the TXT reader with Markdown layout enabled.

- ATX headings (`#` through `######`) render in bold. Level-1 headings use extra line spacing.
- A first-class ATX heading (`# Title`, not `##`) starts a new chapter and a new page.
- Confirm opens the chapter list and jumps to that heading's page.
- The status bar title follows the current `#` chapter.
- Inline markers are stripped for display: `**bold**`, `*italic*`, `` `code` ``, `[text](url)`.
- Lists (`-`, `*`, `1.`), quotes (`>`), and `---` rules are normalized to readable text.
- Fenced code blocks are shown as preformatted lines and do not start chapters.

`FsHelpers::classifyDocument()` returns `DocumentKind::Markdown` for `.md`.

## Not yet rendered

Nested lists, tables, images inside the document, Setext headings, and per-word bold/italic runs inside a paragraph.
