# U1 CDC identity migration

## Lineage and distribution

The functional ABI-2 class keeps its source directory `Drivers/usb_cdc_v2` but
becomes **driver/usb-cdc-acm 0.1.8**. The previous U1 alias source was
**usb-cdc-acm-v2 0.1.7**. Runtime compatibility is still `serial.port@1`;
changing the distribution identity grants no device access.

Published evidence checked October 1:

- [v1.2.19](https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/tag/v1.2.19)
  contains canonical ABI-1 `usb-cdc-acm-0.1.0.t5driver.*`
- [driver-usb-cdc-acm-v2-v0.1.0](https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/tag/driver-usb-cdc-acm-v2-v0.1.0)
  contains the alias ordinary package; its digest agrees with v1.2.19 and
  [release-index 572746f4](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/572746f4fcf3fde19947a066b7e5c8028cd76d21/release-index.json)
- All 268 available index revisions and all 351 local tags carrying a CDC
  manifest showed 0.1.0. Live tag-prefix and manifest-history checks found no
  higher published version. This is targeted evidence, not a claim to enumerate
  deleted tags or every historical asset

0.1.8 is the next numeric increment above the actual U1 source and observed
published lineages. Installation independently compares the candidate against
**both actual on-card versions** and requires a strict increase. An unexpectedly
newer installed generation is refused; an explicit downgrade does not bypass
this migration rule.

Driver Manager advances **1.0.7 -> 1.0.8** (master/published 1.0.6), with
recovery wording that does not falsely claim a rolled-back stage was published.
Current builders, descriptor, manifests, package checks and CI use canonical ID.
Source discovery and exporter reject the retired alias. A future authorized
canonical release-index update removes only the alias row and only after a
strict version comparison; historical records remain readable. No live index,
release, tag, flash or merge is performed by this change.

The ABI-1 proxy and module/runtime sources are retained solely under
`test/drivers/legacy_cdc`, outside production source and driver discovery.
Its old production builder is removed. New loose CDC installations are refused;
the old parser/ELF verifier remains for recognizing existing migration inputs.

## One installer, two protected roots

The existing ordinary ZIP/directory parser, stager, full content verifier and
publisher remain the installation engine. An optional transaction adapter routes
only this explicit historical lineage through a bounded wrapper. There is no
suffix-stripping rule, parallel package registry or second installer.

`ScopedPackageMutation` remains the manager-wide serialization gate. The CDC
wrapper takes exclusive `PackageReplacementLease`s for both exact roots before
checking or moving either generation. Existing mappings and failed-quiescence
pins prevent migration. The publisher's locked internal operation is reused;
callers never recursively acquire the canonical lease.

Canonical ABI-1 loose target/backup inspection and manifest-last cleanup reuse
the prior narrow adapter, now shared by ZIP intake and preview. The alias must
be its ordinary ABI-2 representation; a loose ABI-1 manifest using the alias is
rejected before any rename. Unknown files, corrupt content, unsupported metadata
and unresolved historical `.install`/`.previous` states are retained and refused.

## Durable ordering and restart

After complete staging and revalidation, a bounded local intent records both
versions and SHA-256 fingerprints of the exact candidate and alias manifests.
This binds recovery to the generations that were checked; it is neither a signer
record nor hardware authorization. The intent is exclusively created, closed,
read back and renamed before moving the alias.

1. Alias target -> `.usb-cdc-acm-v2.pkg-migrating`
2. Existing canonical target -> its ordinary `.pkg-previous`
3. Canonical `.pkg-stage` -> target; independently verify its content
4. Purge canonical previous through the existing selective cleanup
5. Check the held alias's retained manifest fingerprint and selectively purge
   it, manifest last; only a genuinely empty manifest-free directory is allowed
6. Remove the matching intent last

With a retained exact candidate stage, recovery restores the canonical previous
and the fingerprint-matching alias. With no stage and an independently verified,
fingerprint-matching canonical target, it finishes committed cleanup. Missing,
changed or ambiguous evidence blocks the two affected identities and preserves
all remaining data. Both fingerprints are checked before restoration/deletion.

A synchronous short-write failure removes only its exclusively created,
definitely closed part. After a crash, a precommit partial record may be removed
only under both leases, with no final intent, holding directory or competing
transaction, and only when its bytes exactly prefix an intent reconstructed from
the fully verified current alias and stage. Unknown record bytes stay untouched.

Boot/provider preparation attempts bounded recovery. Unresolved CDC state does
not disable unrelated services/providers. The installed-root inspection blocks
both affected identities, and provider enumeration excludes those known pending
roots. Driver Manager exposes the retained migration even when no stage remains;
retry reconciles it and discard cannot erase transaction evidence. Uninstall and
old recovery paths honor the same pending state.

## Bounds and evidence

The intent is below 256 bytes. Its manifests are bounded to 4096 bytes. Namespace
presence is checked through a single bounded snapshot per operation, refreshed
after mutations: at most 256 entries, checked names/types, read errors and close
results, item scheduler yields and a two-second cooperative elapsed limit.
Fresh-root absence is checked in the parent directory. A failed boolean exists
query alone is not proof that the alias or intent is absent.

Existing ordinary inventory, entry/total byte limits, hashing checkpoints,
rollback and manifest-last unknown-data protection remain in force. Lower-level
blocked SD-call termination and SdFat malformed-LFN cases without an error flag
remain limitations; this is not a universal media-corruption fail-closed claim.

Host coverage includes both root pins, both version floors, cuts after every
rename, candidate/alias manifest substitution, unknown data, partial cleanup,
record close/short-write faults, recognized crash prefixes, unknown record
preservation, directory iteration errors and canonical-backup cleanup. The SD
wrapper test uses production serialization/SHA/state/filesystem calls while
substituting package verification/purge edges. Actual alias/host/class fixtures,
generic discovery and release-index retirement are also exercised.

Target builds and package/catalog results are recorded for their exact published
SHA in the implementation ledger. No physical power-cut or device qualification
is inferred from host results. Generation-bound installed verification receipts,
coherent inventory caching and lower blocked-I/O termination remain separate U1
work, and final firmware-version reconciliation remains required.

**Implementation In Progress**
