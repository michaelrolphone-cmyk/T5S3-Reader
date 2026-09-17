# Unified Package Manager — MVP test handoff

**Scope:** PR #76, branch `feature/unified-package-manager-mvp`. Use a disposable SD card and development boards. This document separates **host-tested signed package machinery** from the **legacy user-facing installers**. The signed pipeline is not connected to the App Store/Driver Manager UI and is NOT approved to load executable code. Do not interpret a green build or a successful legacy install as production signed-package acceptance.

## 0. Preconditions and evidence

1. Save the currently working firmware and **back up the entire SD card** (including hidden files), plus any configuration you need to preserve. Use a development board and disposable card. Do not test power cuts during firmware flashing or NVS partition maintenance.
2. Check out `feature/unified-package-manager-mvp` at the PR's current head, record `git rev-parse HEAD`, and record the firmware's displayed version. Use that **same commit** for all host/board test results. Do not combine a PR build with applications from a mismatched release.
3. On the host install Python 3, a C++17 compiler, OpenSSL CLI plus development headers/libraries, and PlatformIO. CI uses Linux/Ubuntu; host sanitizer runs may need a comparable environment.
4. Record board model, SD card filesystem/capacity, power source, firmware SHA, installed app/driver release, serial log, test date and the exact failure step. The CI link is evidence for compilation, **not** a hardware test.

## 1. Signed MVP: fully executable host gate

From the repository root:

```bash
bash test/run_unified_package_mvp.sh
```

Expected exit code: **0**, ending in `PASS: host signed-package MVP`. The focused runner compiles with `-Wall -Wextra -Werror`, ASan and UBSan, uses temporary genuine P-256 keys, and exercises the actual signed archive stage/extract/provenance/transaction templates, the device NVS adapter against a fault-injected backend, and an integrated real-signature stage→extract→publish→restart scenario. Signing private keys are ephemeral and are not committed or copied to the SD card.

Check the output for successful signed writer/reader tests, source substitution/short-copy failures, provenance tamper rejection, exact-directory inventory, security-floor rollback, active mapping refusal, rename-cut recovery and failed NVS commit recovery. Any assertion, sanitizer report, build failure or nonzero exit is **FAIL**. Preserve the complete terminal output.

For broader regression coverage:

```bash
bash test/run_driver_test.sh
bash test/run_springboard_test.sh
```

Both must exit 0. The second invokes the signed-fixture tests in the ordinary CI host job. Also check the PR's latest `RiscRTE PlatformIO Build` run: **all three** jobs (host, `t5s3-pro`, `lilygo-epd47-s3`) must conclude `success` for the **same PR head**. An older green run or a superseded/cancelled run is not a green HEAD.

## 2. Firmware compile and smoke gate — each board independently

Build from the same source commit:

```bash
pio run -e t5s3-pro
pio run -e lilygo-epd47-s3
```

Use the normal project flashing procedure for the matching board/environment and capture a serial log at **115200 baud**. Do not flash an EPD47 build to a T5S3 Pro or vice versa. After boot, verify the home UI, SD detection, Apps screen and normal navigation; reboot once and repeat. Record any crash/reboot loop, `dlopen` failure, package-recovery warning, unexpected file deletion or missing app/driver as a failure. A successful CI artifact alone does not prove SD/I/O functionality.

## 3. Existing user-facing installation: transitional smoke, not signed acceptance

On each board, with a working network and test SD:

| ID | Action | Expected observation | What it proves |
|---|---|---|---|
| L1 | Open App Store, scroll its list, install/update a *known-compatible* app from the matching release. | List scrolls, progress completes, version matches; app launches and returns; reboot retains app. | Existing **unsigned** integrity-checked app path only. |
| L2 | Attempt same-version install and a lower semantic-version update where an applicable test release is available. | No silent downgrade or conflicting partial app/manifest pair. Record actual message. | Legacy version/transaction behavior, not signer policy. |
| L3 | In Driver Manager, inspect/install a known-compatible GPS or USB CDC driver release. | Driver is discoverable, version matches, matching ELF loads without unexpected errors; reboot and inspect again. | Legacy driver catalog and package path only. |
| L4 | Open an app that uses the GPS/USB driver; disconnect/reconnect the attached physical device once. | No stale-driver crash or permanently lost discovery; check serial logs and whether the physical device actually works. | Driver/use-gate and device-registry smoke, not signed publishing. |
| L5 | With an ELF active, attempt to update the **same** package only if a safe matching test release exists. | Update must not replace an in-use ELF; unload/close then retry. | Runtime mapping/replacement behavior if the UI actually exercises the shared gate. |

**Do not manufacture a passing L2/L5 result when the UI or release assets cannot exercise that case.** Mark it `BLOCKED`, capture why, and rely on the corresponding host fault tests. The four kinds are implemented in the signed package machinery but services/providers do not yet have a complete user-facing activation/inventory flow. Never claim L1–L5 test the new signed publisher.

## 4. Signed-package negative cases already automated on host

The focused host runner must reject a modified signed byte, altered manifest/signature, changed ELF/resource content, extra or missing directory entries, incompatible ABI/dependency, source substitution, rollback below the persisted security floor and publication with an active mapping. It simulates a reset after the first and second rename, partial backup cleanup and failed floor commit. The expected invariant is **no unverified generation becomes accepted; recover the prior authenticated generation when permitted, otherwise fail closed**. Any test that unexpectedly accepts an altered generation is a stop-ship finding.

On-device execution of this section is **BLOCKED** until production/test firmware has a deliberately provisioned, firmware-controlled signer policy; an approved SD/network caller for `installSignedDevicePackage`; boot-time recovery; and a verified-byte-bound ELF loader. The test-generated public key must not silently become a production trust root, and an SD-supplied public key must not be treated as trusted.

## 5. Destructive/security acceptance — do NOT mark complete yet

These are specification gates, *not instructions to perform hazardous experiments on the current build*:

- Power loss at intake creation, entry write, provenance seal, each target/backup rename, backup purge and NVS floor commit, followed by an authenticated boot recovery.
- Removal/replacement of SD during verification and `dlopen`, proving the loader never maps substituted bytes or retains a dangling mapping.
- Trusted install-intent persistence across reset and malicious SD generation replacement; first-install enrollment versus missing/corrupted NVS; revoked/rotated signing keys.
- Signed SD and online installs through the same real UI/service entry point for app, driver, service and provider; dependency/consent/storage checks, uninstall/quarantine and activation isolation.

The current package use gate prevents **cooperating** renames while mapped, but does not prevent physical SD edits. NVS survives normal restart but is not a hardware monotonic counter under raw-flash rewrite. Until these gates are implemented, PR #76 stays draft and signed publishing must not be promoted as production secure or wired to arbitrary app requests.

## 6. Test report template

Record one row for each host suite, CI job, board smoke check L1–L5, and gated security case:

| Test ID | Commit / board | PASS / FAIL / BLOCKED / NOT RUN | Evidence (CI URL / serial log / console excerpt) | Follow-up issue |
|---|---|---|---|---|
| HOST-SIGNED | | | | |
| HOST-REGRESSION | | | | |
| CI-HOST / CI-T5 / CI-EPD47 | | | | |
| T5-L1…L5 | | | | |
| EPD47-L1…L5 | | | | |
| SECURITY-LOAD / SECURITY-INTENT / SIGNER / FOUR-KIND | | BLOCKED | | |

**MVP test-handoff criterion:** host signed test + broad regression + all three HEAD CI jobs green, and recorded board smoke results with no critical regression. This means the foundation is suitable for *limited developer testing*, **not** that signed installation, hardware security or the roadmap's production MVP is complete. Do not merge or release the signed path solely because this handoff gate is met.
