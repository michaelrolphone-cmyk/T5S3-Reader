#!/usr/bin/env python3
"""Build the additive usb.hid.mouse class ELF using the shared HID builder."""
from build_usb_hid_common import build

if __name__ == '__main__':
    build('usb_hid_mouse', 'usb-hid-mouse', ('usb.hid', 'platform.clock'), 'usb.hid.mouse')
