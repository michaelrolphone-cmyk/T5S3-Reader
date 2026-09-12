# Agent instructions

## Tagged firmware releases

Repository: michaelrolphone-cmyk/T5S3-Reader. Release branch: master.

When the user asks to publish a firmware release, use the existing release-request
mechanism. A workflow-dispatch tool is not required. Read docs/RELEASING.md and
.github/workflows/release.yml for the current implementation before publishing.

1. Inspect the current master branch, existing tags/releases, and CI results.
2. Complete the requested firmware changes and update [crosspoint] version in
   platformio.ini to the intended new version. Verify relevant CI before release.
3. Commit .github/release-request.json on master with enabled=true, tag set to
   the matching new v-prefixed version, and commit_firmware=true (unless the user
   requests otherwise). Commit source changes before or together with the request.
4. This file change automatically triggers Cut release. GitHub Actions builds
   gh_release, commits the merged firmware, creates the tag, and publishes assets.
5. Monitor the push-triggered workflow run for the request commit, inspect failed
   job logs if necessary, and verify the published release and assets before
   reporting success. Provide the release URL. Do not require a manual external
   Run workflow or Publish release action when this mechanism is available.

Existing tags are rejected. Keep the requested version and platformio.ini in
sync. Avoid advancing master during a release with commit_firmware=true because
its firmware commit may otherwise fail to push. Unrelated source commits do not
trigger publishing even when the request file remains enabled.

The mechanism was installed and its disabled-request trigger validated on
2026-09-12 (run 34726065234); this was not an end-to-end publishing test.
Do not publish merely because these instructions exist: act on a user release
request. Repository permissions and available tools must still be checked in
future sessions.

firmware-t5s3-pro.bin and the versioned -app.bin are OTA/SD app images. The merged
versioned .bin is a USB image at 0x0 and must not be used for OTA/SD updates.
