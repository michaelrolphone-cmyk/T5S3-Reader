# Offline X4 paired deployment review

`x4_deployment_plan.py` validates the separate `x4-external-deployment-<sha>`
artifact and writes a dry-run plan. It has no serial imports, device discovery,
flash command or provisioning option. Never pass this bundle to app-only CI.

Supply the expected PR head and archive SHA256 from independently verified
Actions custody (repository, owner, workflow, run/head and artifact identity).
The manifest alone cannot prove that firmware was built from the claimed source.
The validator checks that custody-bound source, paired image hashes and geometry,
ordinary package inventories, and the actual decoded LittleFS bytes agree.
It runs a separately pinned local mklittlefs binary inside a macOS filesystem
sandbox with network denied, writes confined to a temporary decoding directory,
and file-size/CPU/time limits. It never executes code from the artifact.

Example invocation (substitute the reviewed paths and hashes):

```sh
python3 test/hardware/x4_deployment_plan.py \
  --artifact artifact.zip --expected-sha FULL_SOURCE_SHA \
  --artifact-sha256 VERIFIED_ARCHIVE_SHA256 \
  --decoder /absolute/path/to/mklittlefs --decoder-sha256 TRUSTED_TOOL_SHA256 \
  --output dry-run-plan.json
```

A validation pass is **not deployment authorization**. The output retains
`provisioning_authorized: false`, `deployment_state: failure`, and a reason.
Missing stores, mismatched package/store payloads, changed partition geometry,
unsafe archive paths or claimed authorization fail validation. No status is
posted to GitHub by this offline command.

The proposed existing regions are app0 at 0x10000 (maximum 0x640000) and the full
module-store region at 0xc90000 (exactly 0x360000). The plan preserves boot/table/
NVS/OTA metadata below 0x10000, app1 at 0x650000..0xc8ffff, and coredump at
0xff0000..0xffffff. A future separately authorized executor must reverify the
physical X4 MAC and pinned binary partition table, preserve/review current store
contents and rollback evidence, obtain explicit approval for the exact image
pair, and verify bounded writes/readback and protected regions. Building a fresh
LittleFS image does not authorize formatting or replacing an existing region.

The selected boot-store drivers must match ordinary package bytes exactly.
Unselected packages (currently the battery driver) may be distributed alongside
the selected set but are not treated as installed or hardware-validated.

## Explicit T5 software-pair validation

The same offline tool accepts `--board t5s3-pro` for the separately published
`t5-external-deployment-<sha>` artifact. Default identity remains
`xteink-x4-pro`; deployment manifest, firmware marker and decoded board profile
must all match the explicitly selected board. Unknown boards and cross-board
pairs fail. Package integrity, decoded payload equality and fixed partition
geometry checks are unchanged.

A T5 software pass reports no expected MAC or qualified binary partition-table
hash and `physical_binding_established: false`. It cannot borrow the X4 device
binding or backup. Physical T5 identity, live layout and prior-store preservation
remain unverified; this option adds no controller dispatch or hardware access.
