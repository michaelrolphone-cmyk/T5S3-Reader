# Avoid repeated legacy serial-provider activation

## Active claim

Owner: trace_basic_reader_latency. Claim: https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6012159727 . Existing stable report: PERF-20261005-SERIAL-LEGACY-PROVIDER-RELOAD, https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6005930052 . No duplicate canonical ID.

Baseline PR350/xteink-x4-pro-boot: 1cb042921630b7d2f6613c790ee546cf4c26c560 (1.3.155). Isolated branch: perf/serial-provider-rejection-generation. Reserve firmware1.3.157 above separate direct-open1.3.156 reservation. The small direct-open candidate remains separate and is not included here. Final reviewed work targets the existing PR350 through a captured-parent non-force update, not master or another hanging repair PR.

The confirmed trigger is an installed legacy serial.port provider such as usb-ftdi0.1.0, whose base API lacks the inventory extension. T5/non-X4 foreground input refresh repeatedly activates, rejects and releases it. Physical USB device attachment is not required. X4 excludes this foreground hook, though explicit serial APIs can reach it. The user's installed package inventory remains unverified; this is not a claim to have explained all reported ten-minute latency.

A scoped repair will retain a bounded incompatibility observation only after checked release, keyed to the validated provider identity and non-reused metadata snapshot token. Mutation/remount/uncertainty and explicit retry must retire the observation; failed release stays faulted with exact grant ownership. Valid interfaces, device polling, registry publication, authorization, package validation and cleanup must remain unchanged. No FTDI protocol, package payload or public provider ABI changes.

Before implementation, refreshed391 branches/436 all-state PRs and585 unique source heads. InstalledSerialInventory and the FTDI driver each have exactly one historical source variant; no competing fix or claim was found. Twelve graph source/seven header variants and existing reports were checked. Master c765a993 is separate from PR350; PR348 is closed/absorbed. Existing coordination ledger101e2f7f and its sole coordinator remain unchanged.

Next: production-token coherency review, original-cost regression, real packaged payload/foreground path controls, error/retry/cleanup tests and applicable aggregates/target builds. No merge, release, deployment or hardware action.
