# BUG-44: stale OPDS server snapshots after a failed reload

- Repository: `michaelrolphone-cmyk/T5S3-Reader` (`1367546328`).
- Canonical key: `michaelrolphone-cmyk/T5S3-Reader::BUG-44`.
- Baseline and target: `master`, `cff6ef0c11b79634dfa2c688d880df393d8c153c`.
- Repair branch: `fix/bug44-opds-stale-reload`.
- Firmware: `1.3.82` → `1.3.113`, above published firmware and parallel reservations through `1.3.112`.
- No separately distributable app, driver, service or provider changes.
- [Durable isolated claim](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-5979055973).

## Failure and repair

After a successful OPDS load, missing, empty, unreadable or malformed backing
JSON could leave the old server vector live. Native count/read ignored the
failed reload and returned those obsolete records. Firmware list callers also
read the same store after reload, so repairing only the native facade would
leave another consumer stale.

`OpdsServerStore::loadFromFile` now invalidates the prior in-memory snapshot
before reading or migrating. Native count/read also honor the boolean failure;
an active-context read clears its output before attempting the reload. Normal
successful loads, bounded server count and existing migration/persistence
policy remain unchanged. This does not rewrite storage on ordinary failure.

## Deduplication

Checked 401 PRs across all states, 351 live branch refs, 544 distinct source
heads, the current ledger/progress/workflow, PR332 claims and relevant issue
search. All source-containing store variants were identical; both bridge
variants ignored reload failure. The remaining head contained only
`release-index.json`, explaining its source-file 404. No existing repair or
overlapping claim was found. Grok PR384 and other owners' branches were only
read. Canonical coordination remains on the existing shared ledger; this PR
does not write it or supersede its writer.

## Verification at publication

- Complete production bridge plus a stale-store fixture fails the original
  `count == 0` regression on unchanged baseline.
- Complete production store/bridge and exact production JSON-loader function,
  linked against real ArduinoJson 7.4.2, fail on unchanged baseline with
  `failed reload invalidates stale store`.
- Repaired normal and ASan/UBSan focused tests pass: valid replacement and
  field order, empty lists, missing/empty/malformed/read-failed input, repeated
  failure, successful retry, cleared output, invalid API/index/null/inactive
  contexts and failed/retried legacy migration without duplicate old records.
- Independent bridge fixture retains old data while returning false, proving
  the native layer independently rejects failure. Existing OPDS app
  cancel/retry and URL-admission regressions pass.
- Independent read-only review repeated normal, sanitizer, original negative
  and existing OPDS checks and found no blocking defect.
- Full local Springboard aggregate, package-source version guard, Python/shell
  syntax and whitespace checks pass. Native-app aggregate and exact-head
  hosted CI are recorded in the PR checkpoint after publication.
- Both hosted target builds run the new sanitized regression using their own
  real ArduinoJson dependency.

Storage, credential encoding, app-context and persistence endpoints are host
fixtures. No physical SD, network, account, device, firmware flash or hardware
qualification was performed. The production read-size cap, schema validation
and credential encoding are not newly validated. Local LeakSanitizer is
disabled in the caller environment because of the executor's ptrace limit;
ASan/UBSan run, and hosted defaults are unchanged. No full local firmware link
is claimed. Existing persistence/legacy migration defects remain separate.

Integration is **Awaiting merge** once published; it is not fixed on master.
The latest PR description/checkpoint owns final remote SHA and CI results.
No merge, release, deployment, default-branch write or device action is included.
