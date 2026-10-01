# U1 ELF verification call-path and work evidence

## Source audit

The pre-backmerge master1e0188c1 already has the temporary installed-metadata bypass; this
work does not claim that every historical full-tree SHA was first removed here.
The U1 permanent boundary adds actual owned-byte admission and generation-bound
reuse without restoring full-installed-tree hashing to normal UI queries.

- NativeAppHost's managed app selection/inventory calls
  `inspectInstalledOrdinarySdDirectory`; loose pair selection calls
  `inspectInstalledAppPair`
- InstalledCapabilityResolver, InstalledProviderGraph and PackageManager use the
  installed inspection entrypoint. That explicitly passes false for content
  verification; retained capability/inventory snapshots avoid repeated SD scans
  within the same observed quiescent generation
- The corrected loose inspection reads one bounded sidecar and one fixed20-byte
  ELF header, validates declared size, and hashes no ELF bytes. Digestless manual
  compatibility does not acquire an invented content-integrity claim
- Full `verifyAppPair` remains in legacy online staging/recovery. Full ordinary
  directory verification remains in online staging/recovery and uninstall;
  common install/update/readback retains every declared entry's SHA
- Selected cold executable admission hashes the actual owned loader buffer.
  Unchanged memo hits avoid rehash; graph-owned immutable remaps retain separate
  memory-lifetime proof. Neither persisted receipts nor metadata-only queries
  authenticate unobserved external media changes

Explicit full-verification boundaries now advance the existing observed storage
generation before checking bytes, for both canonical and legacy pair paths.
The actual pair/helper regression previously accepted changed bytes from an old
memo after a failed full check; it now refuses them. Successful checks also
retire earlier observations, and invalidation during an in-flight hash cannot
promote the old snapshot to the new generation. This does not reset media or
change task/mutex/handle ownership. Target integration is pending.

A source-wide MD5 audit found the explicit ESP ROM programmer protocol,
KOReader document-ID/authentication protocols, and an optional XTC header field.
No MD5 invocation was found in the app/package/provider/ELF-loader paths above.
These unrelated protocol/format uses remain unchanged.

## Build-scoped measurement

The next necessary board build runs the real AppManifest/AppPackageInstaller/
HalStorage host fixture against that build's actual generated app pairs. It
records package/ELF byte counts, opens, reads, SHA bytes and host elapsed time for
full verification versus installed metadata inspection, plus exact requested
head, compiled checkout, selected source hashes and each input pair's hashes.
The compact `u1-app-verification-work-<board>` artifact is independent of the
full firmware artifact.

This is an in-memory fault-media adapter comparison. Fixture population is
excluded; it is neither a historical pre-U1 firmware benchmark nor physical SD,
menu or launch latency. No arbitrary speed threshold is a new qualification
gate. The local two-pair synthetic smoke passed the reporting path; evidence
from the real built app set and exact target head is still pending.

**Implementation In Progress**
