#!/usr/bin/env python3
"""Build the installed input-to-navigation policy provider."""
from build_usb_hid_common import build

if __name__ == '__main__':
    build('usb_ui_navigation', 'usb-ui-navigation',
          ('input.text', 'usb.hid.gamepad', 'usb.xinput.gamepad'),
          'input.navigation')
