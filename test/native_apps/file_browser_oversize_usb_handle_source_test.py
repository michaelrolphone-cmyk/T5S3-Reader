#!/usr/bin/env python3
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
source = (repo / "Apps/file_browser.c").read_text(encoding="utf-8")

start = source.index("static bool copy_usb_to_sd(")
end = source.index("\nstatic bool copy_selected(", start)
func = source[start:end]

assert "if (!input) return false;" in func
assert "if (!input || source_size > SIZE_MAX) return false;" not in func

guard = func.index("if (source_size > SIZE_MAX)")
close_call = func.index(
    "(void)usb_volume->file_close(usb_volume->context, input, true);",
    guard,
)
status = func.index('"USB file is too large to copy"', close_call)
return_false = func.index("return false;", status)

assert guard < close_call < status < return_false

print("File Browser oversized USB handle cleanup contract PASS")
