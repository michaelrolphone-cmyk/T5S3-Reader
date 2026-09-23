#!/usr/bin/env python3
"""Build the independently installable Xbox 360/XInput gamepad class ELF."""
from build_usb_hid_common import build

if __name__ == '__main__':
    build('usb_xinput_gamepad', 'usb-xinput-gamepad',
          ('usb.host', 'platform.clock'), 'usb.xinput.gamepad')
