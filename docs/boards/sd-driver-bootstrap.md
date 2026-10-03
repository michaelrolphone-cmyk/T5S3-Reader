# SD driver bootstrap — X4 and T5S3

The owner correction supersedes the internal `/bootfs` implementation at
`ec0c099`. SD-based X4 and T5S3 firmware loads ordinary driver packages from SD.
No driver ELF array, internal-flash package store or full storage fallback is
selected. The separately requested no-SD target is outside this path. Prior
flash contents are ignored, not erased. Previous boot/CI results are historical
and do not qualify this placement or its hardware handoff.

## Data and ownership flow

1. `SdPackageBoot` makes one boot attempt. `SdBootReader` owns read-only boot
   access, with no `HalStorage` binding, app VFS, writable interface or provider.
   X4 uses the IDF one-bit SDMMC host on GPIO41/42/40 and the existing active-low
   card power pin. T5 uses `SdSpiCard` sector reads over the already guarded FSPI
   port on GPIO14/21/13, CS12, with LoRa CS46 inactive. It refuses an existing
   SPIClass owner. Normal Board SPI/LoRa setup has not run yet.
2. Both reuse the existing bounded FatFs source with a private symbol prefix,
   read-only configuration and only open/read/close/mount enabled. Its normal
   external-provider build is unchanged. No second runtime filesystem is bound.
3. `/System/Config/boot.json` selects `Drivers/<id>/manifest.json` paths and
   `/System/Config/board.json` establishes board identity. Each selection must
   resolve to that exact ordinary `/Drivers/<id>` directory, with matching ID,
   version, artifact, ABI and capability metadata. SHA-256 checks every declared
   entry, including the source manifest, ELF and exact imports. No manifest
   signature confers authority; existing executor/import/relocation checks apply.
4. Admission copies verified module bytes and metadata into the existing graph,
   pins the ordinary SD roots and leaves acquisition disabled. No hardware is
   activated while bootstrap owns the card/controller. These selected packages
   currently have no resource imports; such a package is rejected at this phase.
5. Every file is checked closed. Filesystem unmount and controller release must
   succeed before `finishBootstrapHandoff` allows graph acquisition. SDMMC deinit
   failure or failed file close retains boot ownership. T5 checks card sync,
   closes card transport, calls guarded `SPI.end`, and verifies no SPI bus handle
   or fault remains. The existing retained-fault policy parks an uncertain SPI
   owner until manual reboot; no timeout authorizes reassignment/reset.
6. The existing provider graph activates storage and its dependency closure,
   and shared `HalStorageVolume` binds the real external `storage.volume` API.
   Home, Apps, Settings, display and input retain their shared software paths.
   T5 Board initialization restores its normal shared SPI transport only after
   boot release. Missing/corrupt packages or failed handoff stop startup; there
   is no second mount attempt or internal-flash rescue.

Bounds: one file at a time, 4 KiB read chunks, 64 KiB JSON, 4 KiB ordinary
manifest, 1 MiB per entry (matching the ordinary archive builder), 2 MiB of
admitted ELFs, 16 selected packages, 15-second file operations and 20-second
mount. The 45-second admission budget is checked before/after reads and between
packages; an in-progress bounded read may overrun that admission deadline before
returning refusal.
Each filesystem operation limits traversal to 1,048,576 steps and disk work to
8,192 sectors (4,096 for mount); sector overhead can reject a large file before
its byte ceiling. FAT checkpoints yield every 256 steps or 8 ms, reads yield per
sector/chunk, and SDMMC commands have at most 250 ms each. T5 protocol waits
are bounded by SdSpiCard and the existing guarded transport/operation deadline.
Boot admission reports each package and the final handoff outcome.

## Ordinary installation, visibility and updates

The board scripts produce `dist/<board>-independent-packages/sdcard` containing:

- `Drivers/<id>`: complete ordinary installed generations.
- `Packages/Inbox/<id>-<version>.rte.zip`: matching ordinary archives, so Driver
  Manager's existing SD Inbox shows each package and its real SD-installed version.
- `System/Config/boot.json` and `board.json`: the board selections/identity.

Boot profiles select IDs, not frozen alternate copies or versioned flash paths.
A coherent updated installed generation is read at a subsequent boot without a
firmware rebuild. Loaded roots remain pinned until successful graph teardown;
ordinary update/uninstall refuses their replacement. Reboot alone does not
install a pending update. There is no automatic boot-update transaction or
promise of hot replacement of the active storage/display dependency closure.
An update must be performed while its generation is safely inactive, using the
existing package transaction flow or separately coordinated staging. Never
force-unload a card owner to update its own backing files.

The historical `build_x4_module_store.py` CLI now emits schema-2 firmware/SD
bundles; its optional `--tool` argument is unused. The complete SD tree, including
hidden `.package.json` files, is packed in `sdcard.zip`; `sd_archive` records its
length/hash and `sd_root` identifies the `sdcard/` prefix inside that archive.
There is no raw tree in the deployment bundle for an uploader to silently omit
files from. The builder round-trips the archive and verifies every staged
ordinary generation against its archive and records hashes. It emits no
`module-store.bin`, flash-driver offset or device-write instruction. CI/controller
integration remains owned by the separate CI task. No filesystem formatting,
partition erasure or device write is performed by these builders.

Firmware changes from 1.3.77 to 1.3.79. Existing external ELF payloads/manifests
are reused unchanged; the boot-only FatFs configuration does not alter their
normal preprocessor configuration. Physical SD handoff validation remains
outstanding on both boards.
