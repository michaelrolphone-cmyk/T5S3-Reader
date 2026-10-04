# OPDS reload regression

Run `python3 test/opds_reload/reload_test.py <ArduinoJson/src> [--sanitize]`.
The target-build workflow supplies its real pinned ArduinoJson dependency.

The harness compiles complete, unchanged production `OpdsServerStore.cpp` and
`NativeOpdsBridge.cpp`, plus the exact production `JsonSettingsIO::loadOpds`
function. File storage, application context, credential encoding, settings and
JSON persistence endpoints are host fixtures. No network, user account or
device is used. These tests do not qualify physical SD behavior or persistence.

Coverage: successful replacement, field order, empty lists, missing/empty/
malformed/read-failed files, repeated failure and retry, output clearing,
inactive/null/index admission, and failed/retried legacy migration. The existing
OPDS bridge admission test additionally retains stale fixture state on load
failure, independently testing failure propagation by the production bridge.

`--source-ref cff6ef0c11b79634dfa2c688d880df393d8c153c` fails with
`failed reload invalidates stale store`, proving the production-baseline bug.
