#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
idf="${USB_CONTROLLER_IDF_SOURCE:-$repo/dist/idf-usb-source/v4.4.7}"
python3 "$repo/test/drivers/usb_controller_bounded_test.py" --control --idf-source "$idf"
