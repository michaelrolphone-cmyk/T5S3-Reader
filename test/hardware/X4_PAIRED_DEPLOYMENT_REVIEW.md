# Offline X4 firmware / ordinary-SD deployment review

`x4_deployment_plan.py` now accepts only the corrected schema-2 firmware/SD
bundle. Schema-1 internal-flash driver stores are retired. The old
`x4_paired_cycle.py` executable and callable entrypoints fail closed before any
artifact/device access, even with old approval flags. Nothing erases old flash.
Historical ec0c099 hardware results do not qualify the corrected SD handoff.

Supply the expected PR head and outer archive SHA-256 from independently
verified Actions custody (repository, owner, workflow, run/head and artifact).
The manifest alone cannot establish that build provenance. The validator checks
source/board identity, firmware hash and app-only offset, fixed partition
geometry, complete nested SD archive hash/inventory, ordinary driver package
hashes/ELF identity, installed bytes and Inbox copies. Hidden `.package.json`
files are mandatory. No external decoder, serial port or device is used.

```sh
python3 test/hardware/x4_deployment_plan.py \
  --artifact artifact.zip --expected-sha FULL_SOURCE_SHA \
  --artifact-sha256 VERIFIED_ARCHIVE_SHA256 --output dry-run-plan.json
```

Validation is not permission to stage or flash. The result keeps
`provisioning_authorized: false`, `deployment_state: pending`,
`physical_binding_established: false` and `hardware_access_performed: false`.
Only app0 at `0x10000` can appear in `proposed_regions`. Boot/table/NVS/OTA,
app1, the former internal driver-store region and coredump are protected.
No firmware array, internal driver image or flash driver offset is accepted.

The boot profile selects ordinary `/Drivers/<id>/manifest.json` generations.
All distributed generations are checked, including the currently unselected
battery package. Validation does not claim they have been physically loaded.
The same read-only tool accepts explicit `--board t5s3-pro` for its schema-2
bundle; it does not borrow the X4 physical identity or partition-table binding.

## Frozen X4 input independently checked on 2026-10-03

- PR350 source: `67d0fd3e9f4012eae681e7d284d2d0bb39732dfb`, firmware 1.3.79.
- Actions run: `37130259920`; artifact: `11276691611`.
- Outer ZIP SHA-256: `ca478cf829e2c78b81f3a4f80134119522d4863ea10404d06f6b006adf180efa`.
- `sdcard.zip`: 268135 bytes, SHA-256 `f55daa10cdcc67eceba27b96e1ff17c4e9acf9c75ad55062bc72954d4332a8d8`.
- `firmware.bin`: 5943936 bytes, SHA-256 `ace416336c25308de46bb865aa952cb0baeab69c90b71f3361b0d8d4071017f1`.
- Exact SD inventory: 50 files, eight hidden package manifests, eight installed
  ordinary driver generations, eight matching Inbox archives and two profiles.

Reject incomplete older artifact `11276770866`. The nested `sdcard.zip` is the
sole staging input, not an uploader-filtered raw directory tree.

## Physical boundary

Mounted-card copying is a separate explicit offline action, with an exact
user-supplied mount path, prior-file backup and readback. It must preserve Apps,
Home pins, books, unrelated Inbox archives and all other user data. Never modify
active driver generations through the test endpoint, bypass the package manager
on a running device, format a card, or guess a volume.

A later app-only device operation requires separate authorization, fresh device
identity/partition checks, exclusive serial ownership, exact image readback,
protected-region verification and bounded cleanup. SD readback and serial
handshakes alone are not physical display/touch/app qualification.
