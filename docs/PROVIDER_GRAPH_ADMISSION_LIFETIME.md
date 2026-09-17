# Provider admission and graph lifetime — current implementation contract

**Normative scope:** [DRIVER_LOADER_SCOPE_CORRECTION.md](DRIVER_LOADER_SCOPE_CORRECTION.md) and the [master package scope contract](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/master/docs/PACKAGE_MANAGER_SCOPE_CONTRACT.md). Draft PR #78 implements physical hardware-owning ELFs, a generic privileged loader, and safe provider lifetimes. Cryptographically signed packages, trusted publisher identity, signed provenance and NVS security floors are NOT mandatory loader prerequisites. Hardware-specific operations remain inside provider ELFs as required by [the hardware-agnostic boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md).

## Registration and authorization

`GraphV2::addVerified` currently accepts only ordinary, nonprivileged specs. A caller-supplied digest, package manifest or plausible import list must never grant privileged OS/CPU imports. `GraphV2::addAuthenticatedPrivileged` is an existing private C++ entry reserved for a future firmware-owned executor; its name comes from the earlier signed-package experiment and MUST NOT be interpreted as a requirement for package signatures. A friend declaration, private C++ method, string or receipt is not by itself an authorization mechanism; native ELFs share an address space rather than an OS sandbox.

The authorized target is a privileged, manager-integrated executor that receives bounded privately owned metadata and exact candidate ELF bytes via the unified manager's ordinary inspection/install path. It independently enforces supported ELF type/architecture, import and relocation bounds, permitted generic privileged ABI symbols, execution-context identity, dependency availability and capability grants. It must not trust arbitrary user-provided metadata as authority. An unsigned package can be admitted once those independent loader/runtime policies are satisfied. Installing a package does not execute it or grant device capabilities. Cryptographic publisher authentication could be added separately only if expressly requested.

## Owned bytes and TOCTOU

`GraphV2::addChecked` validates the input; `OwnedNodeV2` copies identity, path, provided capability and dependency names/interfaces, import strings, expected digest and complete candidate ELF into node-owned memory. Large tables/images may prefer PSRAM. A caller may mutate or free its buffers immediately after registration without changing activation inputs. This is metadata/byte snapshotting, NOT package-signature verification or permission to use privileged symbols.

`ModuleV2::loadVerifiedBytes` copies candidate bytes again, checks the expected digest against that exact copy, validates both symbol tables and relocates the same image. The digest is a consistency/corruption check. The manager/loader must preserve exact bytes for all other legacy path-based `dlopen` uses as applicable, rather than assuming a filename or mutable SD content is immutable. No cryptographic signature or signed byte-lifetime receipt is necessary for the MVP, but integrity, memory lifetime and loader policy must remain correct.

Each node owns its `risc_provider_dependency_v1[]` handed to `start()`. Pinned lower-provider interfaces remain valid through quiesce and stop. Clear them only after safe unload and release of all pins. Failed quiescence quarantines mapped code, metadata, candidate bytes, dependencies and generation-sensitive grants; the manager must not destroy a graph while hardware remains mapped or has consumers. Metadata ownership does not prove ISR/DMA cancellation, native memory isolation or electrical safety.

## Actual state and verification

The branch currently contains signed-profile terminology and a private API whose integration was previously designed around PR #76's experimental `verifySignedProviderProfile` and copied `ProviderProfileReceipt`. Those existing code paths do not establish that the corrected unsigned admission path is implemented. Do not delete independently useful checks or bypass privileged import controls in the course of decoupling them.

`test/run_provider_graph_v2_test.sh` compiles graph/module source with actual loader fixtures and exercises public privilege rejection, private metadata, caller-buffer mutation, snapshot independence, retained dependency table, failed start and teardown quarantine. Experimental physical ELF audits are separate. Those tests do not establish hardware acceptance.

## Remaining acceptance gate

Implement the firmware-private executor against the unified manager's bounded ordinary package inspection; bind package identity, dependencies, ABI/import declaration and exact ELF bytes into owned storage, independently enforce allowed privileged imports and execution-context/resource grants, then activate through the private graph entry. Do NOT require signer keys, cryptographic rollback floors or signed receipts. Retain graph and mapping pins through failed hardware quiescence. Prove physical USB/PHY/I²C ownership cutover, lifecycle and fault recovery on device before publishing experimental physical drivers. Keep this scope focused; any proposed extra trust infrastructure or mandatory dependency requires explicit user authorization.
