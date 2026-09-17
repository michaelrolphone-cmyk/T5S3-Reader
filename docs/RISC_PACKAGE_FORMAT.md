# RISC-PKG v1: deterministic package envelope

Status: **decoder implemented in PR #76; authentication, installation, and writer are not yet connected.** This format is a versioned distribution contract, not a declaration of hardware permissions. Authority: `RISCRTE_PLATFORM_SPEC.md` → `PLATFORM_CAPABILITY_ROADMAP.md` §§26–28 → `SECURITY_ARCHITECTURE.md` and `UNIFIED_PACKAGE_MANAGER_MVP.md`.

## Encoding

All integers are unsigned **little endian**. Every string is ASCII without NUL, length-prefixed with an unsigned byte; non-printing bytes, `/`, `\\`, `:`, dot traversal, noncanonical aliases and out-of-range lengths are invalid. Strings are not Unicode-normalized. Fields and order are fixed: unknown extension bytes and trailing data are forbidden. This first version stores payloads **uncompressed**; no ZIP headers, ZIP paths or decompression ratios are accepted.

A `.risc` file is the exact concatenation:

```text
header[48] | deterministic manifest[manifest_length] | signature[64] | entry[0] | ... | entry[n-1]
```

Header byte offsets:

| Offset | Width | Value |
|---|---:|---|
| 0 | 8 | ASCII `RISCPKG1` |
| 8 | 2 | format version `1` |
| 10 | 2 | signature algorithm `1`: ECDSA P-256 / SHA-256, fixed raw `r[32] || s[32]` big-endian |
| 12 | 4 | manifest length, 16–4096 |
| 16 | 2 | number of entries, 1–16 |
| 18 | 2 | number of capability requirements, 0–16 |
| 20 | 2 | signature length, exactly 64 |
| 22 | 2 | reserved, zero |
| 24 | 8 | sum of all entry lengths |
| 32 | 4 | nonzero trusted-key identifier; key selection never grants capability rights |
| 36 | 8 | complete archive length; must equal actual file length |
| 44 | 4 | reserved, zero |

The manifest begins with a 16-byte fixed prefix:

| Offset | Width | Value |
|---|---:|---|
| 0 | 1 | kind: `0` app, `1` driver, `2` service, `3` provider |
| 1 | 1 | ID length |
| 2 | 1 | version length |
| 3 | 1 | executable artifact basename length |
| 4 | 1 | architecture length |
| 5 | 3 | reserved, zero |
| 8 | 4 | minimum runtime API, nonzero |
| 12 | 4 | security version, nonzero |

The prefix is followed immediately by the four strings `id`, `version`, `artifact`, `architecture`. The ID is lowercase and uses the bounded shared package identity rule. Version is `major.minor.patch`, each component fits in a `uint32`, **no leading zero on multi-digit components**. Artifact is a safe `.elf` basename. Architecture is a bounded lowercase capability-like identifier; compatibility is checked separately against the current device/runtime.

Exactly `entry_count` entry records follow, **strictly increasing in bytewise name order**:

```text
name_length:u8 | executable:u8 (0 or 1) | size:u64 | sha256[32] | name[name_length]
```

Every entry has a nonzero size, a safe lowercase basename, and is within configured entry and aggregate byte budgets. Duplicate filenames (including case/normalization aliases) are rejected. The executable flag is present exactly once and must mark only the declared artifact. Any other `.elf`-suffixed entry is forbidden even if its executable flag is clear. Unknown resource types are not automatically executable. Entry payloads follow the signature contiguously in manifest order, with no padding, offsets or hidden files; their byte lengths must sum to the header's payload length.

Exactly `requirement_count` dependency records then follow, **strictly increasing in bytewise capability name order**:

```text
name_length:u8 | minimum_api:u32 (nonzero) | capability[name_length]
```

Requirements express capability/ABI compatibility, not permission or activation. The manifest must end exactly after the final record. Runtime preflight checks declared runtime ABI, architecture, security floor, entry identity/budgets and capability availability without executing any entry.

## Authentication and publication gate

The signature is over the SHA-256 digest of the literal **48-byte header followed by the complete deterministic manifest**. The raw signature uses fixed 32-byte big-endian P-256 `r` and `s`. A production verifier must resolve `key_id` from an allowlist rooted in firmware/device trust, apply revocation and signer scope policy, verify the signature, and then stream-hash **every** entry against its authenticated digest. Invalid signatures, unknown keys, corrupt entries or noncanonical framing are fatal. Do not treat the decoder's `ReadyForAuthentication` or preflight's `ReadyForContentVerification` as a trust verdict.

A trusted installer must preserve the authenticated byte identity through staging and the eventual `dlopen`: a mutex guarding app-driven renames alone does not protect against removal or replacement of the SD card. Do not publish a candidate until signature, complete contents, installed security-version floor, dependencies, free-space checks and in-use replacement policy all pass. Persist anti-rollback state in trusted storage, not in a mutable SD sidecar. Install must not activate hardware or confer access rights.

## Version evolution and limitations

Unknown format/signature algorithm identifiers, nonzero reserved fields, extra bytes, compressed content and noncanonical metadata are rejected rather than guessed. Future schema versions must define new signed bytes and independent compatibility handling. PR #76 currently has only the bounded decoder and metadata preflight adapter; no key has been provisioned, no signer is implied, and legacy `.elf`/`.json` installation has not been converted to this format. Both SD and online distribution must eventually pass the identical archive bytes through one authenticated installer, with separate explicit development-mode treatment of unsigned legacy packages.
