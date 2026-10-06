# File Browser resume exploration: preserved WIP

## Preservation checkpoint — 2026-10-06

This isolated branch preserves the existing unfinished exploration after the owner requested all in-flight work be committed to its repository and pull request. It is **not acceptable for integration or release**. No implementation repair was added during this preservation pass, and the production PR350 branch was not changed.

Original baseline: `f14196766af6c3bc370d1755d7d523a5bad5650d` (PR350 NativeText/Reader consolidation, firmware 1.3.154). The candidate reduces repeated File Browser startup/resume listings and explores append-only checked directory completion. All six modified tracked files and both new regression files are preserved as found.

## Known blockers

- The legacy application ABI conflates `dir_next(false)` with normal EOF. Removing redundant enumeration can lose incidental recovery from a transient iterator error.
- The exploratory `dir_close_checked` callback checks the current directory error and close result, but does not establish the complete lifetime-safe contract. Child-close failures, retained failed-close ownership, replacement of an unclosed directory in `NativeAppHost::dirOpen`, and USB checked-close handle custody still require review and implementation.
- The full-app resume regression currently fails at `test/native_apps/file_browser_resume_test.c:250`: `opens == scans`. Preservation-pass reproduction compiled successfully with AddressSanitizer/UndefinedBehaviorSanitizer and exited 134 at that assertion. LeakSanitizer was disabled for the executor limitation. The earlier native aggregate log fails at the same test; a previous separate Springboard aggregate log completed successfully. Those prior successes do not establish correctness of this exploratory patch.
- The source retains its historical candidate versions, File Browser 1.3.4 → 1.3.5 and firmware 1.3.154 → 1.3.155, solely to preserve the work. The File Browser reservation was previously released. Firmware 1.3.155 was subsequently used by the BMP improvement, and current PR350 is 1.3.157. These are not current release reservations; reconcile versions against live lineage before any future integration.

## Evidence boundary

Earlier host experiments reported matching normal/open-error/mutation/USB/session/cleanup frames and reduced modeled directory traversal cost. The discovered error/lifetime gaps invalidate readiness despite those samples. Physical hardware, complete target verification, and a passing current full native aggregate are not established. Do not merge, release, deploy, flash, or present this checkpoint as a completed optimization.

The original source patch, host logs, and denied-publication records remain preserved separately in the existing workspace evidence. No scanner or fixer schedule was restarted.

## Original worktree payload hashes

These SHA-256 hashes identify the eight pre-existing files before the preservation note was added:

- `Apps/file_browser.c`: `f389a70d49b82a3ec89672623007dc9bdb93b1fd76bef4c45958ad0ef31238dd`
- `Apps/file_browser.json`: `b74464d3b2fd2c195d54f34211cea6634e51611f908b8272c09af729167eaabb`
- `lib/NativeApps/include/T5AppApi.h`: `1122615f65e1502c5e90ac3d84747a3adc368c571f93be4a66b5f1fd95e67c43`
- `platformio.ini`: `bec502035668f72d6da5bc6a6fb1b3f6f9beba747f81c7cf87cba8181defbe42`
- `src/native/NativeAppHost.cpp`: `7b4c368b0f62a20da1ea4a645019f4567f925ab2098cf502cd4abdf1088d69a8`
- `test/run_native_app_test.sh`: `93af22f017032a64bd57e255d7fecf49b3ffacc9e25725bfc946645ce60fa8c9`
- `test/native_apps/file_browser_resume_scan_test.py`: `4a9a196e09c531fa65950b1dd934772cb76e29a5cd76ef416b4b9a2afed187de`
- `test/native_apps/file_browser_resume_test.c`: `05af6c09aa4a3f2217b7094db1313dd7ad1bc3744cf27d3cb535eb56425d6542`
