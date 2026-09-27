#!/usr/bin/env python3
"""Build the USB HID keyboard to transport-neutral text-input provider."""
from build_usb_hid_common import build

if __name__ == '__main__':
    build('usb_hid_text_input', 'usb-hid-text-input',
          'usb.hid.keyboard', 'input.text')
