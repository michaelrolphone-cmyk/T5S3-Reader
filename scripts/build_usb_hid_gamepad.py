#!/usr/bin/env python3
"""Build usb.hid.gamepad class ELF."""
from build_usb_hid_common import build

if __name__ == '__main__':
    build('usb_hid_gamepad', 'usb-hid-gamepad', 'usb.hid', 'usb.hid.gamepad')
