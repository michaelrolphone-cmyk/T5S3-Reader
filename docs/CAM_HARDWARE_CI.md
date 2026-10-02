# CAM hardware CI pilot

This is a CAM-only pilot. It never operates the T5S3, Heltec, relay, buzzer,
radio or shared USB power. It does not claim a true power-cycle test: the
transport uses ESP32 serial boot/reset. Captured JPEGs remain on the CAM SD
card. The controller stores only output byte counts, hashes and filtered
diagnostics; no image or raw serial stream goes to Git, a PR, Actions or a
GitHub commit status.

## Trust boundary

The repository is public. Do **not** register a repository-scoped self-hosted
runner on the personal Mac. Public PR authors can alter workflow files and
target such a runner. Runner group workflow restrictions apply to organization
or enterprise runners and are not assumed available for this personal repo.

GitHub-hosted `cam-hardware-build.yml` checks out the exact PR head, runs quick
tests and builds a CAM firmware artifact. The Mac uses an independently
installed, reviewed copy of `trusted_controller.py` and `ci_device.py`, pinned
to a specific trusted repository commit. It never checks out or runs PR code.
It automatically accepts only an open, same-repository PR authored by
`michaelrolphone-cmyk`. Each new head SHA is eligible without a comment or
manual dispatch once its cloud build succeeds. The controller also
checks the cloud workflow identity, successful completion, PR association,
bounded artifact entry set, run ID/attempt and SHA-256. Any missing or
ambiguous proof fails closed. External and fork PRs are ineligible. The owner
account and its same-repository branches are trusted sources under this
policy. Their firmware still runs on the CAM; a compromised owner account or
malicious owner-branch commit could affect the device despite the host-script
boundary. The controller uses pinned local device tests; PR edits to those
tests do not execute on the Mac until separately reviewed and installed.

## Candidate and physical transaction

The cloud build uses PlatformIO 6.1.19 and the CAM port's declared platform.
PR #347 now includes merge commits from U1 PR #96 and CAM PR #344, plus the
current master baseline. Thus its exact PR head contains the firmware, CAM
app, host tests and this controller. Those source PRs remain open; Reader
maintainers will reconcile merge order. A detached local merge or prebuilt
image cannot validate the exact-PR-SHA flow.
The artifact contains only `firmware.bin` and its manifest. The SHA in the
manifest is the checked-out PR head, not GitHub's synthetic PR merge commit.

The device suite uses the proven `camera-app-flash-2` boot/capture assertions:
one camera utility output, bounded JPEG byte count, app return zero and steady
heartbeats. It requires CAM USB VID:PID 1a86:7523 at a *privately mapped*
current location and serial port, MAC `28:84:85:4b:57:98`, 16 MiB flash, the known
two-slot OTA partition layout (app0 at `0x10000`, app1 at `0x310000`) and the
installed baseline firmware SHA-256
`e208d9baafc1f8bd9d18eb4b659648e082b44381978d178cc8a44be189515e6d`.
It shares the existing `.device-locks` keys with earlier CAM work. It backs up
the prewrite app0 range to a private evidence directory, flashes and reads back
the exact candidate, runs the bounded suite, then reconnects and restores the
baseline bytes with readback in `finally`. Partition table, NVS and OTA
metadata are hash-checked across the transaction. The fixture reuses a
verified, installed `camera_utility` 0.1.0 package when present; a changed
same-version archive is never used to replace an installed package. Failure to restore is a failure;
the local backup must be kept for manual recovery. There is no relay or hub
power action. USB disconnect, missing board, unexpected firmware or lock
contention all fail before flash.

The prior `2-3.4` / `/dev/cu.usbserial-2340` mapping became stale after USB
reconfiguration. Physical unplug/replug comparison mapped this CAM to
`/dev/cu.usbserial-2330` at `2-3.3`, and a locked read-only preflight verified
its chip MAC and installed baseline. CH340 USB descriptors do not expose the
chip MAC, so a future cabling change requires renewed physical mapping. The
controller rejects a symlink,
world/group-readable binding, wrong MAC and unexpected port form. The device
suite verifies chip MAC and installed baseline before writing; entering an
incorrect USB-UART port can still reset that other board during chip ID, so
physical mapping is a prerequisite.

## Activation boundary

This PR stages source. The owner approved a persistent credential and timer;
the Mac timer is installed but disabled and unloaded pending a successful
end-to-end CAM pilot. The credential, source and physical CAM preflights have
passed. No runner, repository security setting or
required check has been configured. The controller files are pinned at an
exact trusted commit. Activation uses these operations:

1. Create one **fine-grained** GitHub credential owned by
   `michaelrolphone-cmyk` for **all repositories** in that personal account,
   as the owner approved for future projects: Actions **read**, Pull requests
   **read**, Commit statuses **write**, Metadata **read** (automatic). No other write
   scopes are needed. Only the Reader CAM job is configured in the local
   controller allowlist, currently disabled; future repositories need
   separate reviewed jobs.
   Store it privately in the Mac login Keychain as generic-password service
   `riscrte-cam-ci`, account `michaelrolphone-cmyk` using secure local entry.
   Never place it in a shell command argument, chat, repo, plist or log.
   The credential-only read and commit-status probe has passed; it is separate
   from the hardware result.
2. Install a pinned copy of the two trusted Python files from that reviewed
   trusted commit into an isolated local directory with a private Python
   environment (`esptool==4.5.1`, `pyserial==3.5`). The controller and device
   scripts must not be updated from a PR checkout. Point `--evidence-root` to
   a private local directory outside the repository.
3. Enable the installed user-scoped five-minute launchd timer only after the
   private Reader job binding and successful exact-SHA source artifact are
   verified. Its launcher invokes the pinned `trusted_controller.py --scan`
   with a private evidence root and CAM binding. The timer will poll owner PRs, wait
   for their completed exact-SHA cloud build, then post a commit status under
   `CAM hardware / trusted owner SHA` on the exact head SHA. It must remain
   unloaded until credential and
   hardware preflight are approved. The Mac must stay awake and the CAM
   connected for automatic execution.

The credential persists in Keychain with all-personal-repository scope, while
the job allowlist permits only Reader CAM execution. The user launchd timer
persists until removed. The Mac will
automatically flash eligible owner-PR firmware to the CAM and save private
backup/result files. These are the material activation risks. After one
complete request-to-status pilot is verified, a repository ruleset may require
`CAM hardware / trusted owner SHA`
for merges, but that separate security-setting change needs owner approval.
Until then the status is advisory. GitHub permits commit statuses to be
required status checks. Unlike a GitHub App sourced check, a personal-token
status cannot be restricted to one App identity in a required-check rule;
repository writers may be able to post the same context. Review that source
trust limitation before making it a merge gate.

The controller posts `pending` only after verifying the exact cloud artifact,
then `success` only after capture and baseline restoration, `failure` for a
completed failed cloud/device test, or `error` for provenance, transport or
restore uncertainty. A crash with incomplete private evidence leaves the
status pending for manual recovery. The private journal retains exact hashes,
device counts and failure details; the GitHub description carries only short
numeric/hash outcomes. No JPEG or raw serial content is posted.

Use the local `result.json` journal to diagnose failures. A result without
`status_id` means the terminal commit-status post needs retry; the controller retries that
post without re-flashing. An incomplete journal without `result.json` stops
for manual recovery so an interrupted flash is never repeated blindly.
