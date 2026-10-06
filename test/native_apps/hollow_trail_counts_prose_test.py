"""The physical count comparison keeps the original passage and useful hint."""
import ast
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[2]
book = (root / "docs/HOLLOW_TRAIL_NOVELLA.md").read_text()
start = book.index("I found the count while I was turning the trays.")
end = book.index("\n\nIn a drawer under the workbench", start)
expected = book[start:end].strip().replace("’", "'").replace("“", '"').replace("”", '"').replace("*", "")
entries = re.findall(r'^    ("(?:[^"\\]|\\.)*"),$', (root / "Apps/hollow_trail_lore.inc").read_text(), re.M)
body = ast.literal_eval(entries[19])
assert body.startswith(expected)
assert "A silver face reflects the beam around a corner." in body
assert "The black face stops it; an empty frame lets it pass." in body
assert len(body.encode()) < 3800
print("Garden counts: exact original count passage, retained beam guidance and bounded journal text PASS")
