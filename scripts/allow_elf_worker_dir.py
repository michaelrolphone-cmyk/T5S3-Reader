#!/usr/bin/env python3
"""Allow native ELF worker tasks to use session-owned directory listing."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "src/native/NativeAppHost.cpp"

REPLACEMENTS = (
    (
        "bool dirOpen(const char* path) {\n  auto* s = current();\n",
        "bool dirOpen(const char* path) {\n"
        "  // Directory listing is session-owned, not loopTask-owned.\n"
        "  auto* s = session;\n",
    ),
    (
        "bool dirNext(t5_app_dirent_t* out) {\n  auto* s = current();\n",
        "bool dirNext(t5_app_dirent_t* out) {\n  auto* s = session;\n",
    ),
    (
        "void dirClose() {\n  if (auto* s = current(); s && s->directory.isOpen()) s->directory.close();\n",
        "void dirClose() {\n  if (auto* s = session; s && s->directory.isOpen()) s->directory.close();\n",
    ),
)


def apply() -> None:
    text = TARGET.read_text(encoding="utf-8")
    if "Directory listing is session-owned" in text:
        print("NativeAppHost directory listing already uses the ELF session")
        return
    original = text
    for old, new in REPLACEMENTS:
        if old not in text:
            raise SystemExit(f"allow_elf_worker_dir: missing anchor:\n{old}")
        text = text.replace(old, new, 1)
    if text == original:
        raise SystemExit("allow_elf_worker_dir: NativeAppHost.cpp was not patched")
    TARGET.write_text(text, encoding="utf-8")
    print("Patched NativeAppHost directory listing to use the ELF session")


if __name__ == "__main__":
    apply()
