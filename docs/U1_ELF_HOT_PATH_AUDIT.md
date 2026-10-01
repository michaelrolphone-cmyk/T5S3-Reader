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
change task/mutex/handle ownership. Both workflows passed exact730d0773.

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
from the real built app set is now verified at730d0773 (PlatformIO36856309372,
provider36856309324), compiled checkout8bf83b8ddb6119b4056f4929d5b52585b8e642ea.

Both board reports contain38 pairs totaling599,756 ELF bytes. Full verification
hashed599,756 bytes and read611,283 bytes in678 reads; installed inspection
hashed0 bytes and read11,527 bytes in76 reads. Both used76 opens. T5 host times
were2,871us versus1,402us; EPD host times were2,973us versus1,467us. These remain
host adapter measurements, not device timings.

Verified compact artifacts:
- T5 ID11158788646, ZIP SHA256
  `0e17dd3d5aaa996e21257a0f2be6af70d43d0787ca819cd6c8630f498f4c9665`
- EPD ID11159183369, ZIP SHA256
  `b3d665f3ddb047cc374842e4257586143b669cdcdd4ff89aa8e252a03556f418`

Each report's selected source hashes match this published code. Full firmware
artifacts were not downloaded for this measurement.

**Implementation In Progress**
