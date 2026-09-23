# Reusing BQ25896 USB power across boards

A power chip is not a board identity. `Drivers/bq25896/driver.c` implements
the chip's register protocol, source verification, fault handling, restoration
and incoming-power detection. It consumes three installed capabilities:
`i2c.bus@1`, `platform.clock@1` and `board.power.bq25896.profile@1`.
The chip driver alone claims address `0x6b` and writes registers. A profile
only supplies immutable electrical policy; it neither owns I2C nor writes
hardware. Firmware and the USB controller consume `board.power.vbus@1` without
interpreting chip or board data.

The package ID **board-power-t5s3-v2** is retained for updates from existing
installations. Version **0.1.5** replaces **0.1.4** within this unreleased PR.
The source folder and manifest no longer restrict the chip driver to T5S3.
Do not fork that chip package into a second simultaneous register owner.

## Normal installation

After the release containing this change is published, open Driver Manager and
update **board-power-t5s3-v2** normally. Its catalog requirement identifies
`board.power.bq25896.profile@1`; the existing dependency installer downloads
and installs the missing **t5s3-usb-power-profile** package first, then installs
the power driver. No driver editing, manual filename changes, separate profile
installation or compilation is required. An unpublished PR build is not yet
available through the latest-release catalog.

Turn keyboard/controller navigation Off and close USB apps before updating
active USB providers. Update the controller and firmware as described in
[Firmware input navigation](FIRMWARE_INPUT_NAVIGATION.md).

The following profile-authoring information is for developers supporting a
new board; it is not an installation procedure for users.

## Profiles and wiring

`Drivers/t5s3_usb_power_profile` is a separate ordinary package,
**t5s3-usb-power-profile 0.1.0**. Its data preserves the working T5S3 settings:

| Policy | T5S3 value |
| --- | --- |
| Host consumer budget | 500 mA |
| Boost target | 5126 mV |
| Boost peak limit | 1200 mA |
| Initial source settling | 80 ms |
| Input qualification after source-off | 500 ms |
| One cleared startup-fault recovery window | 250 ms |
| Required clean interval after that fault | 200 ms |

The host budget and boost peak limit serve different purposes. Replacing the
1200 mA peak limit with the 500 mA consumer budget previously broke receiver
startup. Live or repeated faults always fail; a profile can disable even the
single cleared-transient recovery by setting both recovery fields to zero.

For another board with the same chip, supply a profile using
`sdk/driver/RiscBq25896ProfileV1.h` and the board's I2C bus provider. Reuse the
identical BQ25896 binary on compatible CPU/ABI targets; other CPU targets need
a rebuild, not a board-specific rewrite. Publish a distinct profile package
with no dependencies and the same profile capability. Select only the applicable
profile for that installation; missing/ambiguous dependencies fail closed.
The `board` metadata is descriptive, not an automatic electrical compatibility
probe. Installing a T5S3 profile on an unidentified board is not supported.

This profile contract covers a BQ25896 directly controlling connector VBUS
with OTG permitted by board wiring. It does not invent pin mappings, select an
I2C controller, configure an external rail switch or claim that every board
containing the chip shares this topology. Additional GPIO/rail dependencies
need a composed power provider using their resource owners. A future charger
or telemetry consumer must share the existing chip owner, not claim `0x6b`
again. No firmware device-specific bridge is needed.

## Validation and lifecycle

The driver copies and validates the profile before claiming I2C. It rejects
incompatible API/size, unsupported voltage steps/current limits, excessive
consumer budgets, inadequate input-qualification time and invalid recovery
intervals. Timing fields have finite limits; they cannot create an unbounded
wait. The read-only REG14 part-number probe rejects a different chip before
any power writes. Chip identity does not prove compatible board wiring.

Register encoding and part identification follow
[TI BQ25896 datasheet SLUSC76C, REG0A and REG14](https://www.ti.com/lit/ds/symlink/bq25896.pdf).
The fixed chip address, ADC conversion bounds and integrated detector's need
for source-off observation remain chip logic. Profile values specify board
margins and electrical choices, without changing the generic monitor ABI.

The profile stays leased as a dependency while the chip driver is loaded.
Turn navigation Off and close USB apps before updating it. Driver Manager
resolves and installs the declared profile dependency before power driver
0.1.5; the user does not perform that ordering manually. Missing profile means
no VBUS sourcing, never fallback to T5S3 constants.
During normal operation, focus handoff retains physical leases; empty-host
role switching retains the chip's I2C claim; actual shutdown releases it only
after source-off and restoration are verified. Failed cleanup retains ownership.
The proven PHY-before-VBUS host startup ordering is unchanged.

`python scripts/build_board_power_t5s3_v2.py` retains its legacy command name
and builds both independent ELFs. Package assembly, canonical export and CI
include both. `test/run_board_power_t5s3_v2_test.sh` checks the real T5 profile,
a synthetic different profile through the same chip code, malformed data,
wrong-chip rejection and existing electrical fault/rollback cases.
`test/run_vbus_provider_chain_v2_test.sh` dynamically loads the real profile,
chip and clock ELFs with a mock I2C ELF and checks dependency lifetime and
missing providers. Alternate-profile simulation is not hardware validation
of any additional board.
