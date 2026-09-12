# Publishing firmware releases

Update `platformio.ini` [crosspoint] version and finish the firmware changes first.
After checking CI, commit this file to master: `.github/release-request.json`.

Example (use a new version matching platformio.ini):
```json
{
  "enabled": true,
  "tag": "v1.0.12",
  "commit_firmware": true
}
```

Only changes to that request file on master trigger automatic publishing.
GitHub Actions validates the request, builds gh_release, optionally commits the
merged USB image into firmware/, creates the tag, and publishes the existing
three firmware assets. No manual workflow dispatch is required.
The build uses the request commit; commit all source changes before or together
with the request. Avoid advancing master during a release when commit_firmware
is true: a non-fast-forward firmware push will fail safely.

The initial request has enabled=false so installing this mechanism validates
the trigger without publishing firmware. Leaving an enabled request in place
does not publish on unrelated source commits. For the next release, change its
tag to the new version. Existing tags are rejected to prevent replacing releases.

The Actions "Cut release" manual workflow remains available with tag and
commit_firmware inputs. It uses the same version and existing-tag checks.
The workflow uses the built-in GITHUB_TOKEN with contents:write; no extra secret
is required. Check the Actions run and release assets before reporting success.

firmware-t5s3-pro.bin and the versioned -app.bin are OTA/SD app images.
The merged versioned .bin is a USB flash image at 0x0, not an OTA/SD image.
