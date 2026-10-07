# X4 bootstrap handoff and unavailable-storage diagnostics

Firmware 1.3.97, narrow PR350 continuation of 1.3.95 at
`9a9925b133778253926ccd288acd8bb3dfb3afc1`. No installed app/driver payload,
profile, GPIO policy or storage mutation changes. Earlier delivered artifacts
remain immutable. This diagnostic revision is not needed to install the already
supplied matching SD packages.

## Observed failure

The supplied startup log showed successful isolated SD boot-package reads,
followed by `storage.volume mounted=0 reason=CMD8 response invalid`. It reported
SD0.2.1, I2C0.1.1, touch0.1.2 and panel0.1.13. Rendering Home did not prove normal
storage access: X4 startup continues into the display/Home path after an
unsuccessful `Storage.bindVolume`.

The 1.3.93/1.3.95 bootstrap cleanup holds active-low SD power GPIO5 HIGH/off after
checked controller release. Matching SD0.2.2 releases this hold at initialization.
Old SD0.2.1 does not. GPIO output writes cannot change a held pad, as documented
by [Espressif's ESP-IDF4.4.7 GPIO contract](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/peripherals/gpio.html#_CPPv412gpio_hold_en10gpio_num_t).
The new firmware and old SD provider therefore cannot perform the normal
power-on handoff. The matching SD-driver package is required for this transition;
this is not an application-manifest or `dir_next` layout incompatibility.

The original generic host replay passed with both SD provider versions because
it began without the bootstrap's held-off power state. That established only
filesystem/app-enumeration compatibility. After modeling the actual handoff,
separately compiled historical0.2.1 failed before sector reads and current0.2.2
released power and mounted. This distinction is preserved rather than presenting
the first replay as full integration coverage.

## Why several views disappeared

- Home pins are read from SD `/Apps/.home_apps` and resolved against installed
  app paths.
- Recent history is read from `/.crosspoint/recent.json` (legacy binary fallback
  remains). Home additionally hides entries when the referenced book path cannot
  be found. These are direct storage reads/stats, not the changed directory loop.
- Fonts and installed battery-provider discovery also need ordinary storage.
  An absent font folder or battery package is independently possible; those
  messages alone are not evidence of mount failure.
- The full image's NVS reset does not directly erase these SD files. No file
  deletion, formatting or card corruption was established by the log.

A source-extracted Home/JSON/app-inventory host experiment confirmed that all
views can disappear with unavailable runtime storage while the card bytes remain
intact, then recover after a checked remount. This is not a claim that the
physical device has recovered. Matching-SD update and subsequent mounted/read
confirmation remain hardware evidence to obtain from the owner.

## Narrow firmware change

The existing firmware-owned Apps launch screen now checks storage readiness
before package recovery/resolution, on returning to the launcher, and after
relevant failures. It shows `SD card unavailable. Check card and matching SD
files.` rather than advising that Springboard or app manifests are missing.
Its original error screen, Back/touch/idle exit behavior and normal missing-app
or loader errors are preserved. No automatic retry, mount, reset, write, erase,
provider-version blacklist, migrated application edit or new driver framework
is introduced.

## Regression coverage

- Actual `SdBootReader::unmount` source with mocked controller/GPIO endpoints,
  then actual SD provider/FatFs/HAL: retained-off bootstrap state, checked release,
  mount/read/write and cleanup. The wire model enforces output hold and power.
- A bounded legacy-transition negative removes only initial hold release from
  current source and must independently fail the same mount assertion. The exact
  historical-provider experiment remains separate from this maintainable test.
- Exact firmware `runNativeSpringboard` source: initially unavailable storage,
  true missing launcher, failure during inventory/resolution/ELF load and child
  launch, existing loader error preservation, button/touch/Home/idle dismissal,
  successful later retry, and existing sleep/resume guards. Original source
  independently fails the unavailable-message assertion after compiling.

Host doubles do not prove physical timings, GPIO voltage, card reliability or
actual recovery. The CMD-low model can reproduce the observed invalid response;
other unpowered electrical levels can time out instead. Local ASan/UBSan is
retained; LeakSanitizer is disabled only for the sandbox's ptrace limitation.
