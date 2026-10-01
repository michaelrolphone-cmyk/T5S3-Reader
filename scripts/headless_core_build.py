"""Stamp the isolated core checkpoint without modifying global tooling."""
import subprocess

Import("env")
root = env.subst("$PROJECT_DIR")
revision = subprocess.check_output(
    ["git", "rev-parse", "HEAD"], cwd=root, text=True
).strip()
dirty = subprocess.check_output(
    ["git", "status", "--porcelain", "--untracked-files=normal"],
    cwd=root, text=True,
).strip()
if dirty:
    revision += "-dirty"
env.Append(CPPDEFINES=[("RISCRTE_CORE_CHECK_REVISION", '\\"' + revision + '\\"')])
