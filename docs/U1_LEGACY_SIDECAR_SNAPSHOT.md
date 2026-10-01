# U1 legacy sidecar snapshot correction

`readAppManifest` now reads one bounded (maximum2048-byte) buffer from one file
handle, checks the exact read and close, and passes those same bytes to the
existing parser. It clears requested outputs before acquisition. It no longer
probes size, closes, and reopens the pathname through a general text reader.

The existing legacy pair adapter consumes AppIntegrity from that parse instead
of reopening/reparsing the sidecar for its size/SHA. This preserves all actual
parser validation and prevents mixing one metadata parse with another set of
digest fields. Stage/recovery verification still hashes every declared ELF byte;
normal installed inspection still does not. Checksum-bearing installed pairs
now apply the cheap declared-size/ELF-header checks that the prior installed
inspection bypass skipped, including refusal of truncated or
wrong-magic files; digestless manual files keep their compatibility contract. Digestless manual compatibility and
existing identities, versions, rename/recovery rules and mapped-app refusal
are unchanged.

The full legacy hash now checks its30-second cooperative budget, yields on16KiB
or8ms checkpoints, throttles progress, and refuses an unsuccessful final close.
It cannot interrupt a lower storage call which never returns; the separate
SD/SPI owner decision remains open.

## Actual-source evidence

The new pair fixture compiles real AppManifest, AppPackageInstaller and
HalStorage with the actual ArduinoJson dependency and fault media:

- Changing the pathname's sidecar on close cannot change the already captured
  parse/digest; a subsequent operation sees and rejects the changed mismatch
- Both sidecar-close and executable-close failures refuse verification, with
  existing discarded-close uncertainty retained
- A full verification attempt invalidates previous generation-bound observations
  before checking. The actual helper refused changed bytes after failed SHA,
  refused stale/in-flight promotion, and recovered warm reuse after a new valid
  verification. The prior same-epoch memo accepted that failure case
- Full SHA still processes every ELF byte on successful verification; deadline
  exhaustion refuses incomplete work; installed inspection adds zero ELF hashes
- A valid digestless manual pair remains usable, while digest-required staging
  refuses it

The mutation regression fails on exact baseline64d19d64's probe/reopen parser.
Using the corrected parser with that baseline's old pair adapter also fails at
its second sidecar read. The complete correction passes both boundaries.
These are host fault results, not proof against every unobserved external media
edit or a physical power-cut test. The complete native-app aggregate and the10 existing installation integration
checks also passed. Both normal board jobs run this fixture through
the existing actual-parser step. A second mode measures full versus metadata
work on the actual built app set; see U1_ELF_HOT_PATH_AUDIT.md. Target integration
passed exact730d0773 in workflows36856309372 /36856309324.

**Implementation In Progress**
