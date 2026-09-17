# Unified Package Manager MVP — scope correction

**Normative for PR #76 as of September 17, 2026.** User requested a unified package manager, not cryptographically signed packages. This correction takes precedence over mandatory-signature, signer-trust, security-floor and signed-only load/release requirements stated in `UNIFIED_PACKAGE_MANAGER_MVP.md`, `RISC_PACKAGE_FORMAT.md` and the older roadmap. Those documents remain truthful records of the experimental code and signed-format design, but they do not define the MVP acceptance criteria. See the current master [`PACKAGE_MANAGER_SCOPE_CONTRACT.md`](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/master/docs/PACKAGE_MANAGER_SCOPE_CONTRACT.md).

## Authorized implementation and acceptance

- One package identity/model and a common managed install/update/uninstall/inventory/recovery engine for app, driver, service and provider packages.
- Same package input/semantics for online downloads and offline SD installation. No desktop signing or key provisioning prerequisite.
- Bounded structure/ELF/ABI checks, path safety, declared SHA-256 **integrity** verification, explicit dependency/compatibility diagnostics, crash-consistent staged replacement, preservation of previous generation on failure and in-use replacement refusal.
- Installation never automatically activates a driver or grants hardware access; generic runtime execution-context, capability and privileged-import controls remain independently enforced.
- Host and physical acceptance of four-kind lifecycle and both source types; report tests without conflating CI with hardware acceptance.

## Explicitly deferred, not blocking

No required P-256 signatures, trusted publisher identities/keys, signing service, signer allowlists/revocation, signed provenance, device NVS cryptographic security floors, anti-rollback key policy, signed-only package acceptance or signed-only ELF admission. A payload SHA-256 is useful for detecting corruption but is not a signature or publisher authentication. Do not replace signatures with another mandatory trust infrastructure by renaming it.

## Existing code and required cleanup

This branch currently implements extensive signing, provenance, NVS-floor and authenticated-publication experiments. Their presence does not make them user requirements. Preserve independently useful parsing, integrity, preflight, staging, recovery, ownership and lifecycle functionality, but decouple every normal installation/activation pathway from mandatory cryptographic trust and remove signing-specific release gates. Keep signed-format code isolated/archived for separately approved future work or remove it after verifying no needed generic functionality is lost. The current UI is not migrated to the unified manager: changing this document is **not** completion of that work.

Do not turn PR #78 into a blocker waiting for a signed profile. The manager can supply bounded, privately owned checked metadata and the exact candidate bytes to the loader; loader import/relocation grants and capability permissions must remain independently enforced. The format of the unsigned package envelope can be selected from existing compatible assets instead of requiring the experimental RISC-PKG v1 signature field. This is a spec correction only: existing source behavior remains unchanged until code is modified and tested.

## Change control

In the PR description enumerate explicit user objective, in-scope deliverables, deferred/excluded items, acceptance tests and independently justified dependencies. Any new cryptographic trust system, persistent security policy, breaking format, cross-PR prerequisite or added release gate requires explicit user approval. Roadmap features and best-practice arguments are not approval. If a scope expansion appears, stop extending it, restore the stated MVP, and move the proposal to a separate nonblocking plan.
