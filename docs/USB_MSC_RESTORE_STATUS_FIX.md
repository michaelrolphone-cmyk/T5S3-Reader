# MSC cleanup restore check (X4 .1.78)

The combined X4 receipt `8e9e8eb7a2b1f7f860777d0d0e8a89dd2822af87`
selects the shared MSC source `45cf61ac013fb618e8d7fed63217a35350484d52`
and TinyUSB `1eb6ce784ca9b8acbbe43dba9f1d9c26c2e80eb0`.
Its MSC 0.1.5 ELF SHA-256 is
`f791783ac4e21d61c0da4576aef091af2d218d7cd84c4e6ac2f0f36b09a00dac`,
identical to the recorded .055 recovery artifact and both .077 input images.

`risc_msc_transport_stop` reset the controller, restored registers, then compared
all of `USB_WRAP.test_conf`. ESP32-S3 IDF v4.4.7 `usb_wrap_struct.h` declares
bits 0–3 R/W controls and bits 4–6 RO receive status. Saved `0x05`, observed
`0x75` therefore falsely failed cleanup even with zero reset timeouts. The
provider correctly retained its media and PHY leases after that false failure;
repeated Stop could encounter the same mismatch.

Compare only the four writable controls. Keep reset/AHB idle, pad drive,
OTG configuration, routing, GPIO selectors and clock checks intact. The provider
still stops the controller before ending SD export, then releases the PHY.
The package remains `usb-device-msc-esp32s3`, version 0.1.5 -> 0.1.6.
No ABI, pump, polling cadence, Windows eject detection or directory-read change.

## Regression checks

`python3 test/run_usb_msc_transport_test.py --soc-include <framework>/tools/sdk/esp32s3/include/soc/esp32s3/include`
compiles verbatim production stop and wait bodies with simulated MMIO, the real
SDK USB register layout and shared route restoration. The model supplies reset
completion at the wait boundary; it is not physical-controller evidence.
The old source fails the first `0x05`/`0x75` assertion. The fixed source covers
1,024 saved-control/receive combinations, all four writable-bit failures,
reset/AHB idle failures, pad/configuration/clock failures and successful retries.
`SANITIZE=1` enables UBSan for this focused test.

`TINYUSB_SOURCE=<pinned-checkout> bash test/run_usb_device_msc_test.sh`
checks production provider and TinyUSB BOT/SCSI with transport/storage doubles.
The new `eject-stop-retry` case waits for eject CSW completion, retains SD/PHY
through repeated failed shutdown, then allows ordinary Stop and exactly one
remount/release. Existing stop/remount failure cases now exercise retry too.
The unchanged pending-I/O, owner, stale token, suspend/unconfigure, sync-failure,
read/write and prevent-removal cases remain safety coverage.

Physical Windows eject signaling, SDMMC transactions and CDC enumeration need
hardware validation after integration. Do not infer safe eject from suspend or
unconfigure, remount while controller custody is uncertain, or bypass Home's
ownership guard. The fix removes one demonstrated false cleanup failure; it
does not establish that Windows always sends a usable eject signal.
