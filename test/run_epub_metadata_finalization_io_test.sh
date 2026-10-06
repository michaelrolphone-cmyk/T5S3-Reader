#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 -B test/epub_metadata_finalization_io/test_baseline_fetch.py
exec python3 test/epub_metadata_finalization_io/run_test.py "$@"
