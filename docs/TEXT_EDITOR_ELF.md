# Text Editor ELF — USB HID end-to-end acceptance app

`Apps/text_editor.c` is a first-class native app, not a firmware HID handler. It acquires the versioned generic provider-grant bridge and subscribes to `usb.hid.keyboard` from the installed keyboard provider. USB discovery, class operations and input decoding remain inside separate USB ELFs. The editor uses the normal native app rendering API and the existing `T5StorageApi` atomic file-write service. It can run only with matching firmware containing `t5_provider_capability_get_api` and the independently installed USB HID provider dependency stack. This PR does **not** merge, publish, install or flash it.

## Setup

1. Install firmware built from the same revision of this branch after its build is green, and install its approved/qualified USB controller, host, HID and HID keyboard provider packages in dependency order. Driver build success does **not** qualify board USB power, physical HID operation or signed admission; follow the device's existing admission process.
2. Build `Apps/text_editor.c` through `python scripts/build_native_app.py Apps/text_editor.c --output dist/apps/text_editor.elf --require-manifest`, or the branch's normal `scripts/build_all_apps.py` build. Install **both** matching `text_editor.elf` and `text_editor.json` under `/Apps/` on the SD card. The manifest requires RiscRTE 1.2.48 or later and declares optional `usb.hid.keyboard` API >= 1.
3. Open **Text Editor** on the device. The editor starts with an **untitled**, unsaved buffer so it never overwrites an existing notes file by default. Press the device's Confirm button to acquire the keyboard provider and establish an input subscription. Attach a standard USB HID **boot keyboard** and look for the `Keyboard connected` status. A failed acquire gives a visible `HID unavailable` error; it does not fall back to firmware USB.
4. Use `Ctrl+N` to name a new file. Documents are deliberately confined to `/sd/Documents` with filename characters `[A-Za-z0-9_.-]` and `.txt`/`.md` extension. The directory must exist for the file to be written by the underlying atomic storage API. Use `Ctrl+O` to browse existing documents.

## Keys

| Shortcut | Behavior |
|---|---|
| Ordinary US-layout keys, Shift, Caps Lock, Enter, Tab | Enter ASCII text, newline, tab. |
| Left, Right, Up, Down; Home, End | Move cursor; Shift extends a selection. |
| Backspace, Delete | Delete selected text or adjacent character. |
| Ctrl+A / Ctrl+C / Ctrl+X / Ctrl+V | Select all, copy, cut, paste from the app's clipboard. |
| Ctrl+S | Atomically save the current file; prompts for a name if untitled. |
| Ctrl+Shift+S | Save As a different filename; existing target is never silently overwritten. |
| Ctrl+O / Ctrl+N | Browse documents / create a new document. |
| Esc or device Back | Exit/cancel; dirty documents prompt **S**ave, **D**iscard or **Esc** cancel. |

The first version intentionally offers **plain text** only, including for Markdown documents; no preview or syntax highlighting. It only accepts printable ASCII plus LF and tab, rejects binary/non-ASCII files rather than risking corruption, normalizes CRLF to LF, and refuses documents above **16 KiB**. Its clipboard limit is 2 KiB, and the directory picker scans at most 128 entries and displays 64 matching files. It does not implement key-repeat, Unicode, NKRO keyboard layouts, external editors, or a general filesystem picker.

## Hardware acceptance checklist

- Verify that Confirm acquires the installed provider with no HID handler in firmware; attaching a boot keyboard changes status to connected.
- Create `hid-test.md`, type a Markdown heading, a paragraph with Shift and punctuation, and several lines. Check Backspace, Delete, arrow navigation, selection, copy/paste and `Ctrl+S`.
- Exit the editor, reopen `hid-test.md` with `Ctrl+O`, and verify its contents persist byte-for-byte. Check that opening a >16 KiB or invalid file preserves the current buffer.
- Edit and attempt `Ctrl+O` or exit without saving. Cancel, save, and discard paths must behave as indicated. Save As must not overwrite a different existing document.
- Unplug and reattach the keyboard while editing. The disconnect message must appear and a new attachment must resume inputs. Exit the app while the keyboard remains attached, relaunch, press Confirm, and check the driver can acquire it again without unplugging; no lingering subscription or VBUS ownership.
- Failure cases: absent driver, unsupported keyboard profile, queue overflow, storage error, and missing Documents directory must show an error rather than claim success.

Host regression: `bash test/run_usb_hid_test.sh` includes `text_editor_core_test` (selection, edit, CRLF, invalid-content preservation, 16 KiB boundary). CI/native ELF validation and physical board testing are separate gates.
