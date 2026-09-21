#!/usr/bin/env python3
"""Build usb.hid class ELF."""
from build_usb_hid_common import build

if __name__ == '__main__':
    build('usb_hid', 'usb-hid', 'usb.host', 'usb.hid')
