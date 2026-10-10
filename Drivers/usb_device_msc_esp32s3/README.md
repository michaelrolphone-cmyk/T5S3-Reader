# ESP32-S3 SD export as a USB device

`usb-device-msc-esp32s3` 0.1.3 publishes `usb.device.msc@1`. The computer is the
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
overflow also faults. An owner poll performs at most 32 ready-work passes, checking a 2 ms admission
budget before each pass and returning immediately when no hardware or queued
work is ready. Each pass consumes at most 32 hardware FIFO entries and one
queued stack event. No sleep is inserted between ready packet/stack passes.
The app retains responsibility for its scheduler yield after polling. This is
not a preemptive 2 ms deadline: an already entered synchronous SD operation
finishes under its existing storage bounds/yields before the pump returns.

## Host and filesystem behavior

Full-speed USB, one removable LUN, 512-byte sectors, read/write10, inquiry,
capacity, request sense, mode sense, synchronize cache and prevent/allow removal
use TinyUSB's BOT/SCSI state machine. Entire read/write geometry is checked
before the first sector; reserved CBW fields, malformed command lengths,
nonzero LUNs and non-512-byte command geometry fail closed. Each successful SD
write is checked synchronously before its USB status is sent. Failed/uncertain
writes are never retried. A media I/O error permanently freezes further read,
write and sync callbacks for that session but leaves a healthy USB controller
servicing bounded protocol work: failed CSW, REQUEST SENSE and BOT reset. Bus
reset, reconfiguration, suspend and resume never clear that media fault or
remount the card. Hardware/controller faults and queue overflow independently
stop all protocol work. An enabled DWC2 IN timeout is explicitly acknowledged
and latched as a controller fault before any simultaneous completion can be
reported successful. Explicit checked cleanup remains the only ownership
return path.

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

## Protocol diagnostics

The additive, size/tag/version-checked diagnostics suffix returns an owner-task,
RAM-only snapshot. It exposes command start/completion counters, opcode/tag,
LBA/block count, requested bytes, latest completed CSW status, actual successful
block counts, last storage result/LBA/count and elapsed time, poll gaps, stall
count and the last pump pass count. It neither polls USB nor touches SD. The
latest completed record can coalesce commands between app snapshots; counter
deltas make this visible. Configured USB means enumeration completed, not that
the host mounted a filesystem. No sector contents or credential data are logged.

The regression suite uses the production provider, patched TinyUSB BOT/SCSI and
fake DCD. Its media-fault tests failed against 0.1.2 before the correction and
now check failed status, sense, reset and retained custody. The production pump
policy also has deterministic ready-packet counts: eight ready 64-byte packets
require eight old single-pass polls versus one new poll. This scheduling model
does not establish wire timing or throughput when the next packet is not ready.

A separate register-image regression executes the actual staged DWC2 handler
with a bulk IN timeout, alone and together with transfer completion. The frozen
handler left timeout-only input healthy; the corrected handler acknowledges the
W1C timeout and fails closed without a success event. This source reproduction
does not show that a timeout occurred during the observed Windows stall.

## Active-command inter-packet service (0.1.4)

Full-speed 64-byte packets are separated by host-token/FIFO gaps. Previously,
`OwnerPump.h` returned at the first gap even with READ10/WRITE10/CSW active;
the caller then slept, turning each packet gap into a scheduler wait. The pump
now waits for the next masked controller/stack event only while a BOT command
is active. No new thread, interrupt, DMA, raw mounted access, buffer, retry or
native change is introduced.

Each call remains bounded by 32 serviced passes and 2 ms before admitting the
next pass. A separate 8,192-probe ceiling bounds a nonadvancing clock; rollback
returns immediately. Inactive idle does no busy-wait. A controller fault stops
service. Bus reset/unplug clear activity via the event hook; BOT class reset
and deconfiguration clear it from their actual TinyUSB reset routines without
inventing a successful CSW or releasing SD custody.
The caller still yields. An already entered synchronous SD callback retains
its own timeout, as before; this patch does not make that timeout preemptible.

`test/run_usb_device_msc_test.sh` includes delayed packets, idle, paused host,
clock rollback/freeze, delayed reset/unplug and controller fault/MMIO cases.
The deterministic 64 KiB model at 65 microseconds per packet and a 1 ms caller
sleep changes from 1,024 pump calls / 1,028,096 modeled microseconds to 34 calls /
100,005 modeled microseconds. These are synthetic scheduling counts, not a
hardware throughput measurement or proof of the observed Windows hang's cause.

`test/run_usb_directory_test.sh` joins the real provider/TinyUSB to an external
X4 0.2.13 SD fixture and real FatFs. Set `TINYUSB_SOURCE`, `RISCRTE_READER_ROOT`,
`X4_SD_FIXTURE_ROOT` (the selected X4 minimal/test directory) and `X4_SD_SDK_ROOT`
(the prepared SDK); `X4_SD_DRIVER_SOURCE` optionally selects an exact SD source.
It creates a RAM-backed partitioned FAT32 volume through mounted file APIs,
closes/unmounts it, reads 322 entries and 962 LFN slots over 81 fragmented
clusters via USB READ10, exercises 64 KiB/high-LBA reads, checks exclusive
ownership and a whole-image hash, then remounts and verifies file bytes.
Physical USB, native SD timing and Windows remain hardware qualification work.
