#!/usr/bin/env bash
set -euo pipefail
f=test/run_native_app_test.sh
if grep -Fq 'koreader_document_id/document_id_test.py' "$f"; then
  echo already wired
  rm -f .github/workflows/fold-407-runner.yml .github/fold-407-runner.sh
  git add -A .github/workflows/fold-407-runner.yml .github/fold-407-runner.sh 2>/dev/null || true
  if ! git diff --cached --quiet; then
    git config user.email 'michael.rol.phone@gmail.com'
    git config user.name 'michaelrolphone-cmyk'
    git commit -m 'temp: remove fold-407-runner one-shot helpers'
    git push origin HEAD:fix/consolidated-bugs-20261003-c
  fi
  exit 0
fi
python3 - <<'PY'
from pathlib import Path
p = Path("test/run_native_app_test.sh")
text = p.read_text()
needle = "WRAP_TEST_SANITIZE=1 python3 \"$repo_dir/test/text_wrap_regression.py\"\n"
insert = needle + "python3 \"$repo_dir/test/koreader_document_id/document_id_test.py\" --sanitize\n"
if needle not in text:
    raise SystemExit("needle missing")
p.write_text(text.replace(needle, insert, 1))
print("wired koreader_document_id into native runner")
PY
rm -f .github/workflows/fold-407-runner.yml .github/fold-407-runner.sh
git add test/run_native_app_test.sh
git add -A .github/workflows/fold-407-runner.yml .github/fold-407-runner.sh
git config user.email 'michael.rol.phone@gmail.com'
git config user.name 'michaelrolphone-cmyk'
git commit -m "Fold #407 BUG-74: wire koreader_document_id into native app runner"
git push origin HEAD:fix/consolidated-bugs-20261003-c
