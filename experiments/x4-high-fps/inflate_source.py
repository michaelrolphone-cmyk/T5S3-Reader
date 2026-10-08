"""Inflate the reviewed standalone X4 laboratory source before compilation."""

from __future__ import annotations

import gzip
import hashlib
from pathlib import Path

Import("env")  # noqa: F821  # type: ignore[name-defined]

EXPECTED_SHA256 = "6610ecc249ad3fb0da6996129ef77bdcc46357b7141a7ab01174f469abcd942f"
project_dir = Path(env.subst("$PROJECT_DIR"))  # noqa: F821  # type: ignore[name-defined]
payload = project_dir / "experiments" / "x4-high-fps" / "source" / "full_main.cpp.gz"
generated = project_dir / "experiments" / "x4-high-fps" / "generated" / "full_main.inc"

with gzip.open(payload, "rb") as source:
    content = source.read()

digest = hashlib.sha256(content).hexdigest()
if digest != EXPECTED_SHA256:
    raise RuntimeError(f"X4 lab source digest mismatch: expected {EXPECTED_SHA256}, got {digest}")

generated.parent.mkdir(parents=True, exist_ok=True)
if not generated.exists() or generated.read_bytes() != content:
    generated.write_bytes(content)

print(f"Inflated X4 high-FPS laboratory source: {generated} ({len(content)} bytes)")
