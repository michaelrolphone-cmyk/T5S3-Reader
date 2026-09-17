# RISC-PKG v1 signed provider profile

**Authority:** [RISC-PKG canonical archive](RISC_PACKAGE_FORMAT.md), [RiscRTE platform](RISCRTE_PLATFORM_SPEC.md), [driver architecture](RUNTIME_DRIVER_ARCHITECTURE.md), [hardware-agnostic boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md) and [privileged OS/CPU ABI](PRIVILEGED_OS_CPU_ABI.md) where present on the integration branch. Draft package manager PR #76 implements the profile verifier; USB driver PR #78 implements the private ELF import matcher but the branches are not yet integrated. This format does not itself grant any hardware rights.

## Signed structure

Use the existing deterministic RISC-PKG v1 P-256 manifest, **not** a second ad hoc signature or a format-version change. A privileged driver/provider package must be of signed kind `driver` or `provider` and contain exactly one executable ELF and two mandatory nonexecutable resource entries in the *same* signed archive:

```text
driver.elf                    -- declared executable; signed SHA-256 and length
provider-abi.v1               -- signed SHA-256; exact canonical profile below
privileged-imports.v1         -- signed SHA-256; canonical exact import set
```

The artifact basename can differ from `driver.elf`, but must match the archive's declared executable entry. Additional ordinary resources can be included under the archive's normal rules. Each entry name, size, SHA-256, package kind/ID/version, architecture, minimum runtime API, security version, signing key ID and sorted dependency requirements is covered by the **same** signed header + manifest. Resource names must be exactly `provider-abi.v1` and `privileged-imports.v1`, with no aliases or uppercase variants.

`provider-abi.v1` is ASCII, exactly three LF-terminated lines in this order, with no BOM, whitespace, extra keys or CRLF:

```text
os-cpu-abi=1
provides=usb.controller
api=1
```

`provides` is a nonempty canonical, lower-case, bounded capability ID (up to 63 bytes); `api` is canonical positive decimal uint32 without leading zeros. The example is a driver-specific value, **not** a USB branch or hardware policy in the generic package parser. `os-cpu-abi=1` selects the generic 46-symbol privileged kernel/CPU compatibility contract from PR #78, not resident firmware hardware operations. A future ABI uses a new explicitly supported version/profile rather than interpreting unknown keys.

`privileged-imports.v1` is 1–128 nonempty, strictly sorted and unique ASCII symbol names, each 1–127 bytes, with one LF per line including the last. Current v1 accepts `[A-Za-z0-9_]` only; no NUL, blanks, comments, spaces, CRLF or extra lines. Generate this resource from **both** linked ELF symbol tables using PR #78's `scripts/generate_privileged_imports_v1.py`, not from a manually guessed firmware export list. The private native loader independently verifies exact set equality against both ELF tables on its digest-checked private byte snapshot **before mapping**. A name allowed by the ABI but absent from the signed declaration is denied.

Example builder arguments (using appropriately named local files, an externally provisioned signing key and matching runtime/driver metadata):

```bash
python scripts/build_risc_package.py \
  --kind driver --id usb-controller-esp32s3 --version 1.0.0 \
  --artifact driver.elf --architecture xtensa-esp32s3 \
  --min-runtime-api 2 --security-version 1 --key-id 7 \
  --private-key /secure/off-repo/signer.pem \
  --entry driver.elf=./driver.elf \
  --entry provider-abi.v1=./provider-abi.v1 \
  --entry privileged-imports.v1=./privileged-imports.v1 \
  --require i2c.bus:1 --output ./provider.risc
```

The sample key ID/version/API values are illustrative, not production provisioning or an authorization policy. Production private signing keys must not reside in the repository/build artifacts.

## Verified profile extraction

`src/runtime/packages/PackageProviderProfile.h::verifySignedProviderProfile()` accepts an already-opened archive reader, the manager-retained SHA-256 fingerprint from **earlier authenticated intake**, the firmware-owned signer verifier and runtime, dependency and rollback-floor checks. It verifies the archive's real P-256 signature and **every** entry digest, requires the three signed entries and canonical profile, stream-parses and hashes the import resource with at most 512-byte reads, and copies identity, signer key ID, security version, architecture, requirements, capability, ABI, import names, executable size/digest and the pinned prefix fingerprint into a caller-owned ~17 KiB metadata receipt. A second full verification and identical-prefix comparison rejects SD substitution during extraction. Failures clear the archive/receipt rather than leaving a partially trusted object. The receipt must be allocated in manager-owned long-lived memory, not on a small task stack.

**The receipt is data, not a capability or unforgeable authorization token.** Its public fields cannot establish provenance if populated by an ordinary caller. Only a private manager entry point that itself invokes real signing and policy verification may hand it to the provider executor. The executable is NOT pinned by these metadata checks: activation must copy the executable from manager-controlled source into private RAM, compare SHA-256 against the signed digest in the same receipt, validate exact imports on that copy, relocate precisely that copy with the one-shot module grant, and retain code/dependencies until hardware quiesces. Recheck current signer revocation, security floor, trusted installed generation and per-execution-context permission when making the activation decision. Do not treat a package requirement as authorization.

## Integration and acceptance

- PR #76: wire the signed profile verifier to manager-controlled intake/installed-generation inspection and preserve a manager-owned receipt for the whole activation window. Enforce signer kind/ID scope and anti-rollback at that private API, not in archive-controlled fields.
- PR #78: consume the verified profile only in the privileged executor, pin its strings/requirements/import list and source lifetime, compare the signed executable digest on a private copy, verify actual ELF imports, then acquire generic capability/resource leases. Reject direct application-authored `SpecV2` for privileged registration.
- Physical acceptance: prove exclusive USB/PHY/VBUS and I²C0 ownership after shutting down legacy owners, real T5S3 driver start/stop, power/backfeed/role tests and complete ISR/DMA/callback teardown. Native ELF import filters are not memory isolation.

Real P-256 cross-language fixture `test/resources/package_provider_profile_test.py` signs a valid profile and malformed alternatives with temporary keys. It tests missing entries, wrong ABI/capability/API, noncanonical or oversized imports, signature/payload tampering and substitution by a *different valid signed* package with the wrong previously retained intake fingerprint. Neither host fixtures nor signed metadata constitute on-device hardware qualification. Keep both PRs draft and their experimental driver ELFs unpublished/noninstallable.
