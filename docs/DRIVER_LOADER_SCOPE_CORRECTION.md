# Physical ELF driver/loader — scope correction

**Normative PR #78 amendment, September 17, 2026.** The objective is to move the entire physical hardware implementation into installable provider ELFs while retaining a generic hardware-blind RiscRTE core. The user did NOT authorize requiring cryptographically signed packages. This amendment overrides mandatory signed admission and signed-profile acceptance gates in `USB_ELF_MIGRATION_STATUS.md`, `PRIVILEGED_OS_CPU_ABI.md`, PR descriptions and any inherited older package design. Read the current master [`PACKAGE_MANAGER_SCOPE_CONTRACT.md`](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/master/docs/PACKAGE_MANAGER_SCOPE_CONTRACT.md).

## Required for this driver work

- PIC-linked hardware-owning USB/PHY, host, CDC/CP210x, VBUS, I²C and clock provider ELFs, with generic provider dependencies, lifecycle and safe exclusive hardware ownership; no resident proxy and no compiled normal-operation fallback.
- Generic, bounded private OS/CPU import ABI; reject undefined or unsupported imports, malformed relocations and unauthorized privileged symbol use before mapping. Loader-issued, module-specific relocation grants and execution-context/capability permissions stay in force regardless of package source.
- Privately own/copy candidate metadata and actual executable bytes during loading; validate ELF layout, architecture, length, import set and optional digest against the *same bytes actually mapped*. The digest is a corruption/consistency check, not publisher authentication and must not independently grant privilege.
- Take candidate identity, dependencies, import declaration and executable reference from the manager's bounded validated package metadata, not arbitrary untrusted caller fields. Independently enforce privileged-import policy in the loader, rather than assuming authenticity from a signed declaration.
- Real-board start, hardware I/O, hotplug, safe teardown, exclusive handoff from legacy firmware owners and appropriate negative tests. Clearly distinguish CI/audits from hardware qualification.

## Explicitly deferred, nonblocking

P-256 package signatures, signer trust roots/scopes/rotation/revocation, signed provider profiles/receipts, retained signed provenance and NVS cryptographic security-version floors. These must not block ordinary manager installation or physical ELF activation. `provider-abi.v1` and `privileged-imports.v1` may be useful generated declarations; they do not need to be signed and cannot be treated as authority by themselves. If a currently implemented code path requires a signed receipt, its replacement with source-independent manager-validated metadata is *outstanding implementation work*, not an accepted prerequisite.

## Existing state versus target

This branch currently implements signed-import terminology and assumes a cross-PR signed admission gate. Leave the truthful existing-state narrative intact until the source is changed, but do not report the signature path as a required acceptance criterion. The new code must provide a working unsigned-package route with the same structural checks and independent privileged-runtime restrictions. Do not weaken OS/CPU ABI import containment or physical ownership merely to remove signature dependencies. This document modifies specification, not executable behavior.

## Change-control gate

Before adding any new loader/package prerequisite, state its direct connection to the user's current objective, why a simpler bounded mechanism is insufficient, and whether it is REQUIRED, OPTIONAL or DEFERRED. An adjacent roadmap/security feature cannot become a release gate without explicit approval. Track optional trust/authentication proposals independently and keep PR #78 focused on functional hardware ELFs and generic loader behavior.
