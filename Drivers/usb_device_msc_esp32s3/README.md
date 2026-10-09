# ESP32-S3 SD export as a USB device

`usb-device-msc-esp32s3` 0.1.2 publishes `usb.device.msc@1`. The computer is the
USB host. This is separate from the existing `usb_mass_storage` host class,
which consumes an attached USB drive. Shared USB implementation stays Reader.
There are no changes to PR350, Runtime USB protocol code, or a board charger.

## Composition and custody

The ordinary ABI2 singleton has three explicit dependencies:

- `platform.usb.phy.resource@1`: native owner-task identity and a generation-safe
  exclusive lease for the internal PHY shared with HWCDC. The ESP32-S3 controller
  kind is checked. The native owner must fence diagnostics, HWCDC sleep recovery,
  app exit, restart and sleep before returning a lease; same-task SD operations
  must remain available. A failed release retains every resource.
- `platform.clock@1`: a bounded 30 ms detach interval after native console
  suspension, including when the computer was attached before the first boot.
- `storage.volume@1`: the tagged `RiscStorageExportV1.h` optional tail. SD owns
  logging flush/pause, zero-file/directory admission, checked sync/unmount,
  physical CSD capacity, raw blocks and checked local remount/logging resume.

Package registration/start does not activate the PHY or export the card. `begin` acquires a PREPARING SD lease only. The tagged preparation suffix
advances one complete log transaction per explicit step. Ordinary `poll` does
no SD or USB work while preparing, so display/input waits can remain responsive.
The app settles its preparation frame before each step and processes Cancel
first. Only after the SD provider reports READY does the MSC provider claim the
native PHY and start the controller. Cleanup reverses all fallible custody:
stop/reset USB and clear its queue; sync/remount SD; release native PHY. A failed
stage does not release later dependencies. Retained custody with no token is
terminal until reset. Local handles are refused, never silently closed.

The transfer app holds its grant and session token and polls every few
milliseconds. SD and TinyUSB callbacks run synchronously on this same Runtime
owner task. There are no USB tasks, registered ISRs, DMA, borrowed app callbacks,
heap allocations, native USB imports, or provider-BSS atomics. TinyUSB's bounded
queue is retained in ELF RAM. Its DWC handler is called directly during `poll`.
Controller waits have 100,000 CPU-poll bounds; a failure latches an error. Queue
overflow also faults. One service pass consumes at most 32 hardware FIFO packets
and 32 queued stack events; SD's existing per-operation bounds/yields apply.

## Host and filesystem behavior

Full-speed USB, one removable LUN, 512-byte sectors, read/write10, inquiry,
capacity, request sense, mode sense, synchronize cache and prevent/allow removal
use TinyUSB's BOT/SCSI state machine. Entire read/write geometry is checked
before the first sector; reserved CBW fields, malformed command lengths,
nonzero LUNs and non-512-byte command geometry fail closed. Each successful SD
write is checked synchronously before its USB status is sent. Failed/uncertain
writes are never retried.

Host eject (`START STOP UNIT` with LoEj=1 and Start=0) checks sync. Local remount
waits until the successful eject CSW has actually completed, then stops the USB
controller and returns SD custody. A real controller session-end event follows
checked cleanup too. Host unconfiguration, bus reset and suspend retain SD
ownership; they do not prove safe eject.

X4 has no verified VBUS detector. It cannot distinguish every unplug from USB
suspend. Therefore silence never triggers remount. If the host did not eject,
the app may use `END_CABLE_REMOVED` only after the user explicitly confirms the
cable is physically removed. Native reset cannot recover data still cached on
the computer and never transmitted; ejecting on the computer is the normal path.
The provider preserves acknowledged writes, does not format, and keeps uncertain
media frozen. Blank/unsupported filesystems may be exported, but local recovery
reports `MEDIA_UNAVAILABLE`; it never formats them.

Stop before the first USB configuration is cancellable. After configuration,
ordinary Stop refuses until eject/session-end. Retained cleanup can be retried
explicitly without admitting new USB/media work. The app must block navigation
and sleep until `end` succeeds, even after an input or display failure.

## Source and build

TinyUSB 0.16.0 is pinned to `1eb6ce784ca9b8acbbe43dba9f1d9c26c2e80eb0`
(https://github.com/hathach/tinyusb). `prepare_usb_device_stack.py` copies that
clean source into the output and applies exact checked anchors: cooperative DCD
hooks, bounded hardware waits and event loop, queue-fault handling, explicit
post-reset stack cleanup and strict MSC command geometry. Original MIT license
headers remain in every staged upstream source; the staged source receipt hashes
the six original C inputs. The shared Reader `PhyRoute.h`, `phy_gpio.c` and
export map are reused. ESP32-S3 MMIO addresses come from the framework's SoC
linker map and are compiled as numeric addresses, never ELF-relative pointers.
Device-mode matrix setup follows IDF v4.4.7 `components/usb/usb_phy.c`; selectors,
PHY mux, wrapper registers, clock and pad strength are saved and checked on
restoration. Native code still owns console suspension/resumption.

Build using the pinned Arduino-ESP32 2.0.17 package and an ESP32-S3 compiler:

```sh
python3 scripts/build_usb_device_msc.py --tinyusb /path/to/tinyusb-0.16.0 \
  --framework /path/to/framework-arduinoespressif32 \
  --cc /path/to/xtensa-esp32s3-elf-gcc --output /new/output/path
TINYUSB_SOURCE=/path/to/tinyusb-0.16.0 SANITIZE=1 bash test/run_usb_device_msc_test.sh
```

The build rejects nonlocal USB/OS/weak imports, unexpected exports, unsafe atomics
and unmappable relative pointers. It emits the ordinary ELF, manifest, copied
source hashes, symbol/disassembly listings and target proof. The prototype uses
Arduino-ESP32's default 303a:0002 VID/PID; release identity requires an assigned
product identity. No serial number is synthesized from user or device data.

Host tests drive production TinyUSB control, BOT, SCSI and provider lifecycle
through a fake DCD, including actual packet read/write/eject. They do not model
electrical behavior, interrupt timing, USB enumeration on a computer, or X4
throughput. A target build is not hardware qualification; no flashing is done.

## Transaction-safe preparation

The append-only SDK suffixes preserve the v1 table prefixes. A preparation token
freezes local admission and ordinary log drains. Its source high-water mark is
captured before the app can emit new preparation diagnostics. Later trace text
stays pending for local drainage after cancellation/eject; it cannot keep the
current export preparation alive indefinitely. Each explicit SD prepare step
uses the existing 15-second hard transaction guard, at most one <=4095-byte
append and checked close, or final media sync and FatFs unregister. No total
backlog deadline aborts a later open transaction. Poll never advances preparation.

Clean preparation refusal attempts checked cancellation and reports
MEDIA_UNAVAILABLE with a final session acknowledgment still required. Uncertain
SD failure retains the session and prohibits all host admission. Bounded error
text includes the exact storage result and the first SD/logger failure; the app
emits it through ordinary diagnostics while the native console is still owned.
Legacy storage export without the preparation suffix is rejected at activation.
