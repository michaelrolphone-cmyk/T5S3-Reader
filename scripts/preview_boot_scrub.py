#!/usr/bin/env python3
"""Render the production scan commands as an idealized 1s portrait GIF.
Usage: python3 scripts/preview_boot_scrub.py /absolute/output.gif
Requires a host C++ compiler and Pillow. This is not an optical panel model.
"""
import subprocess
import sys
import tempfile
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="boot-wisp-") as temporary:
    path = Path(temporary)
    executable = path / "preview"
    subprocess.run(["c++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                    str(root / "scripts/preview_boot_scrub.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable), str(path)], check=True)
    frames = [Image.open(path / f"frame-{i}.pgm").convert("RGB") for i in range(25)]
    durations = [600] + [40 if i % 6 else 50 for i in range(1, 25)]
    frames.append(frames[-1].copy())
    durations.append(800)
    frames[0].save(sys.argv[1], save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, optimize=True)
