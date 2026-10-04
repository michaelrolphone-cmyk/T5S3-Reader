#!/usr/bin/env bash
set -euo pipefail
git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"

cleanup() {
  rm -f .github/workflows/fold-397-398.yml \
        .github/fold-397-398.sh \
        .github/fold-397-398.b64.part0 \
        .github/fold-397-398.b64.part1 \
        .github/fold-397-398.b64.part2 \
        COMMIT_MSG.txt
  git add -A
  if ! git diff --cached --quiet; then
    git commit -m "temp: remove fold-397-398 one-shot workflow"
    git push origin HEAD:fix/consolidated-bugs-20261003-c
  fi
}

if grep -q 'version = 1.3.108' platformio.ini \
   && test -f docs/bugfix/BUG-162.md \
   && test -f docs/bugfix/BUG-51.md \
   && test -f src/network/SettingsJsonWriter.h \
   && test -f test/web_settings/settings_response_test.py \
   && test -f test/recent_books/migration_test.py \
   && grep -q 'SettingsJsonWriter' src/network/CrossPointWebServer.cpp \
   && grep -q 'recent_books/migration_test.py' test/run_springboard_test.sh \
   && grep -q 'url_resolution_test.py' test/run_springboard_test.sh; then
  echo "already folded"
  cleanup
  exit 0
fi

# Prefer tar payload if present and real (not PLACEHOLDER)
if test -f .github/fold-397-398.b64.part0 && ! grep -qx 'PLACEHOLDER' .github/fold-397-398.b64.part0; then
  cat .github/fold-397-398.b64.part* > /tmp/fold.b64
  python3 - <<'PY2'
import base64, tarfile, io
from pathlib import Path
data = base64.b64decode(Path('/tmp/fold.b64').read_text().strip())
with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as tar:
    tar.extractall('.')
    print('extracted', tar.getnames())
PY2
  grep -q 'version = 1.3.108' platformio.ini
  test -f docs/bugfix/BUG-162.md
  test -f docs/bugfix/BUG-51.md
  MSG=$(cat COMMIT_MSG.txt)
  rm -f COMMIT_MSG.txt .github/workflows/fold-397-398.yml .github/fold-397-398.sh \
        .github/fold-397-398.b64 .github/fold-397-398.b64.part*
  git add -A
  git commit -m "$MSG"
  git push origin HEAD:fix/consolidated-bugs-20261003-c
  echo "Pushed $(git rev-parse HEAD) via tar payload"
  exit 0
fi

# Fallback: cherry-pick source commits
git fetch origin fix/bug162-web-font-family-settings fix/bug51-preserve-recent-migration-source

resolve_pio() {
  local ver="$1"
  python3 - "$ver" <<'PY2'
import re, sys
from pathlib import Path
ver = sys.argv[1]
p = Path('platformio.ini')
t = p.read_text()
pat = r'<<<<<<< HEAD\nversion = [^\n]+\n=======\nversion = ' + re.escape(ver) + r'\n>>>>>>> [^\n]+\n'
t2, n = re.subn(pat, f'version = {ver}\n', t, count=1)
assert n == 1 and '<<<<<<<' not in t2, repr(t[:500])
p.write_text(t2)
PY2
  git add platformio.ini
}

if ! test -f docs/bugfix/BUG-162.md; then
  set +e
  git cherry-pick 4a40da788fbd355fde58455ddea87bf818fd5f5f
  st=$?
  set -e
  if test "$st" -ne 0; then
    if grep -q '<<<<<<' platformio.ini; then
      resolve_pio 1.3.107
      GIT_EDITOR=true git cherry-pick --continue
    else
      echo "cherry-pick 4a40da78 failed" >&2
      git status; exit 1
    fi
  else
    sed -i -E 's/^version = 1\.3\.[0-9]+$/version = 1.3.107/' platformio.ini
    if ! git diff --quiet platformio.ini; then
      git add platformio.ini
      git commit --amend --no-edit
    fi
  fi

  set +e
  git cherry-pick 3d992fdd47a2cdc77db784c0f1844dc0bc77cc4f
  st=$?
  set -e
  if test "$st" -ne 0; then
    if test -f .git/CHERRY_PICK_HEAD; then
      if git status | grep -qi empty; then
        git cherry-pick --skip
      elif grep -q '<<<<<<' platformio.ini; then
        resolve_pio 1.3.107
        GIT_EDITOR=true git cherry-pick --continue
      else
        git add -A
        GIT_EDITOR=true git cherry-pick --continue || git cherry-pick --skip
      fi
    else
      echo "cherry-pick 3d992fdd failed" >&2
      git status; exit 1
    fi
  fi
fi

if ! test -f docs/bugfix/BUG-51.md; then
  set +e
  git cherry-pick e91ac2a0d55cd84100027513a2cc5855cdb01110
  st=$?
  set -e
  if test "$st" -ne 0; then
    if grep -q '<<<<<<' platformio.ini; then
      resolve_pio 1.3.108
    fi
    if grep -q '<<<<<<' test/run_springboard_test.sh; then
      python3 <<'PY2'
from pathlib import Path
import re
p = Path('test/run_springboard_test.sh')
t = p.read_text()
start = t.index('<<<<<<< HEAD\n')
m = re.search(r'>>>>>>> [^\n]+\n', t[start:])
assert m, 'missing end marker'
end = start + m.end()
chunk = t[start:end]
assert 'url_resolution_test.py' in chunk or 'migration_test.py' in chunk
replacement = (
    'python3 "$repo_dir/test/util/url_resolution_test.py" --sanitize\n'
    'python3 "$repo_dir/test/recent_books/migration_test.py" --sanitize\n'
)
t2 = t[:start] + replacement + t[end:]
assert '<<<<<<<' not in t2
p.write_text(t2)
PY2
      git add test/run_springboard_test.sh
    fi
    git add -u || true
    GIT_EDITOR=true git cherry-pick --continue
  else
    sed -i -E 's/^version = 1\.3\.[0-9]+$/version = 1.3.108/' platformio.ini
    if ! git diff --quiet platformio.ini; then
      git add platformio.ini
      git commit --amend --no-edit
    fi
  fi
fi

sed -i -E 's/^version = 1\.3\.[0-9]+$/version = 1.3.108/' platformio.ini
if ! git diff --quiet platformio.ini; then
  git add platformio.ini
  git commit -m "ci: set rollup firmware to 1.3.108 after #397/#398 fold"
fi

grep -q 'version = 1.3.108' platformio.ini
test -f docs/bugfix/BUG-162.md
test -f docs/bugfix/BUG-51.md
test -f src/network/SettingsJsonWriter.h
test -f test/web_settings/settings_response_test.py
test -f test/recent_books/migration_test.py
grep -q 'SettingsJsonWriter' src/network/CrossPointWebServer.cpp
grep -q 'recent_books/migration_test.py' test/run_springboard_test.sh
grep -q 'url_resolution_test.py' test/run_springboard_test.sh

cleanup
echo "Pushed $(git rev-parse HEAD) via cherry-pick"
