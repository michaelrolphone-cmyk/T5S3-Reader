# Unified RiscRTE package envelope — current MVP specification

**Normative scope:** [UNIFIED_PACKAGE_MANAGER_SCOPE_CORRECTION.md](UNIFIED_PACKAGE_MANAGER_SCOPE_CORRECTION.md). The previous P-256-specific RISC-PKG v1 document is preserved verbatim as [the deferred signed-format experiment](experimental/RISC_PACKAGE_FORMAT_SIGNED_V1.md); it is NOT the format required by the Unified Package Manager MVP. This specification changes requirements only. Existing implementation currently still contains signed-only APIs; do not claim an unsigned installer works until the code is decoupled and tested.

## Goal and compatibility

One package is a distribution unit for one of four explicit kinds: `application`, `driver`, `service`, `provider`. The manager must accept the same package semantics from offline SD and online download, using one parser/preflight, installation transaction, inventory and lifecycle. Preserve working legacy ELF/JSON assets where possible; avoid making a new archive format or a desktop packaging utility a prerequisite. A package may be a bounded envelope or a validated manifest plus referenced payload, provided both sources are handled through identical manager logic and file validation. Choose one minimal canonical on-disk representation during implementation, document it precisely, and provide migration/compatibility for existing assets. Do not claim a finalized byte layout where none has been accepted or implemented.

## Required metadata

- Bounded kind, stable safe package ID and canonical version; target architecture and compatible runtime/ELF ABI range.
- One identified executable ELF where applicable, plus bounded resource names, lengths and optional/declared SHA-256 content digests. Reject duplicates, traversal, aliases, missing/extraneous files and unsupported ELF/ABI forms. Hashes detect accidental corruption or content mismatch, not publisher identity.
- Versioned mandatory/optional dependency capabilities and compatible provider requirements. Declaring a capability never grants permission or activates hardware.
- Installation metadata sufficient for a recoverable target directory, previous generation, inventory and uninstall. Do not execute candidates while merely reading a package manifest.

## Common processing

Read bounded bytes from an already-authorized source; validate structure, identity, architecture/ABI, declared versions, requirements, paths, file lengths and content digests. Stage only in manager-owned temporary paths, reread/verify staged output, refuse replacement of active mapped packages, publish with recoverable previous-generation handling, verify installed files and update inventory. Failures must preserve earlier working installs, avoid deleting unmanaged data and emit actionable diagnostics. Manager operation never grants hardware capabilities; loader import/relocation gates, user consent, execution context and physical driver ownership are separate controls.

Offline installation must work without network or desktop-side Python/signing. Online packages go through the same inspection and installation engine, not a separate publisher. Implement install/update/uninstall and recovery for all four kinds and validate on device.

## Explicit exclusions

**No mandatory signatures or signing infrastructure.** P-256, trusted signer IDs, allowlists/revocation, signed-provenance files, cryptographic anti-rollback and NVS security floors belong to a separate future proposal requiring explicit user approval. They must not be enforced by the ordinary MVP, nor required to activate a physical ELF provider. A standalone integrity digest is NOT cryptographic authentication; do not use digest text or arbitrary manifest import claims as authorization for privileged symbols. Loader policy remains independently enforced, using privately owned checked metadata and executable bytes.

The archived signed RISC-PKG v1 byte layout and code are experimental current state. Any optional signed-format support must remain isolated and nonblocking; it must not prevent installation of normal supported packages. Do not silently switch the MVP to a different mandatory trust scheme.

## Acceptance and change control

Verify both SD and online paths, identity/dependency/compatibility failures, valid/corrupt/truncated payloads, stage/rename interruption, restart recovery, active-ELF replacement refusal, inventory, update and removal for each package kind. Distinguish host/CI tests from actual hardware validation.

Any extra requirement must identify its direct basis in the explicit user request, state REQUIRED/OPTIONAL/DEFERRED and have a test. A broad roadmap item or nearby security spec is not permission to expand this MVP. New trust roots, signing keys, security floors or signed-only release gates require explicit authorization and must not be dependencies of the unified manager or hardware ELF loader.
