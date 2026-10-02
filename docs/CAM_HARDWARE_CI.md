# CAM hardware CI pilot

This is a CAM-only pilot. It never operates the T5S3, Heltec, relay, buzzer,
radio or shared USB power. It does not claim a true power-cycle test: the
transport uses ESP32 serial boot/reset. Captured JPEGs remain on the CAM SD
card. The controller stores only output byte counts, hashes and filtered
diagnostics; no image or raw serial stream goes to Git, a PR, Actions or a
GitHub check.

## Trust boundary

The repository is public. Do **not** register a repository-scoped self-hosted
runner on the personal Mac. Public PR authors can alter workflow files and
target such a runner. Runner group workflow restrictions apply to organization
or enterprise runners and are not assumed available for this personal repo.

GitHub-hosted `cam-hardware-build.yml` checks out the exact PR head, runs quick
tests and builds a CAM firmware artifact. The Mac uses an independently
installed, reviewed copy of `trusted_controller.py` and `ci_device.py`, pinned
to a specific trusted repository commit. It never checks out or runs PR code.
It accepts only an open, same-repository PR authored by
`michaelrolphone-cmyk`, with an exact-head owner comment:

```
/cam-hardware approve <40-character PR head SHA>
```

The owner may comment on their own PR; GitHub does not allow self-review
approval. A new commit requires a new exact-SHA comment. The controller also
checks the cloud workflow identity, successful completion, PR association,
bounded artifact entry set, run ID/attempt and SHA-256. Any missing or
ambiguous proof fails closed. The firmware still runs on the CAM, so source
approval is a meaningful device-safety gate, not a sandbox for firmware.

## Candidate and physical transaction

The cloud build uses PlatformIO 6.1.19 and the CAM port's declared platform.
It requires CAM source plus the U1 package runtime in the PR checkout. This
workflow will fail until those currently separate sources are integrated on
the PR under review; it does not substitute a stale prebuilt firmware image.
The artifact contains only `firmware.bin` and its manifest. The SHA in the
manifest is the checked-out PR head, not GitHub's synthetic PR merge commit.

The device suite uses the proven `camera-app-flash-2` boot/capture assertions:
one camera utility output, bounded JPEG byte count, app return zero and steady
heartbeats. It requires CAM USB VID:PID 1a86:7523 at location `2-3.4`, serial
`/dev/cu.usbserial-2340`, MAC `28:84:85:4b:57:98`, 16 MiB flash, the known
partition layout and the installed baseline firmware SHA-256
`e208d9baafc1f8bd9d18eb4b659648e082b44381978d178cc8a44be189515e6d`.
It shares the existing `.device-locks` keys with earlier CAM work. It backs up
the prewrite app range to a private evidence directory, flashes and reads back
the exact candidate, runs the bounded suite, then reconnects and restores the
baseline bytes with readback in `finally`. Failure to restore is a failure;
the local backup must be kept for manual recovery. There is no relay or hub
power action. USB disconnect, missing board, unexpected firmware or lock
contention all fail before flash.

## Activation boundary

This PR only stages source. It does not create a GitHub credential, register a
runner, configure launchd, change repository security settings or impose a
required check. After reviewing and merging this PR, activation needs an
explicit approval for these exact operations:

1. Create one **fine-grained** GitHub credential for only
   `michaelrolphone-cmyk/T5S3-Reader`: Actions **read**, Pull requests **read**,
   Issues **read** (PR comment gate), Checks **write**, Metadata **read**.
   Store it privately in the Mac login Keychain as generic-password service
   `riscrte-cam-ci`, account `michaelrolphone-cmyk` using secure local entry.
   Never place it in a shell command argument, chat, repo, plist or log.
2. Install a pinned copy of the two trusted Python files from the reviewed
   master commit into an isolated local directory with a private Python
   environment (`esptool==4.5.1`, `pyserial==3.5`). The controller and device
   scripts must not be updated from a PR checkout. Point `--evidence-root` to
   a private local directory outside the repository.
3. Load a user-scoped launchd timer that invokes that exact local Python and
   `trusted_controller.py --scan --evidence-root <private-directory>` at a
   bounded interval such as five minutes. The timer will poll owner PRs, wait
   for their exact approval and completed cloud build, then post a Checks API
   result on the exact head SHA. It must remain unloaded until credential and
   hardware preflight are approved. The Mac must stay awake and the CAM
   connected for automatic execution.

The credential persists in Keychain and can create/update checks in this one
repo; the user launchd timer persists until unloaded. The Mac will flash
reviewed PR firmware to the CAM and save private backup/result files. These
are the material activation risks. After one complete request-to-check pilot
is verified, a repository ruleset may require `CAM hardware / reviewed SHA`
for merges, but that separate security-setting change needs owner approval.
Until then the check is advisory.

Use the local `result.json` journal to diagnose failures. A result without
`check_id` means the Checks API post needs retry; the controller retries that
post without re-flashing. An incomplete journal without `result.json` stops
for manual recovery so an interrupted flash is never repeated blindly.
