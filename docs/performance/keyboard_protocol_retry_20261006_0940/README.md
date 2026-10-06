# Boot-keyboard protocol failures repeat on every foreground poll

Report-only performance scan, scheduled 2026-10-06 09:40 UTC.

**One newly confirmed conditional software defect.** Stable alias: `michaelrolphone-cmyk/T5S3-Reader::PERF-20261006-KEYBOARD-SET-PROTOCOL-RETRY`. No canonical number, implementation claim, version reservation, repair branch or repair PR is assigned. This report does not explain the owner's ten-minute symptom, establish which peripherals were attached, or claim measured device latency.

## Exact source and scope

Repository ID 1367546328. Default branch is `master`, verified at `c765a9931e2f1772d1dd3b5870362c18a08a9e57`. Tested source is open/unmerged [PR350](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/350), branch `xteink-x4-pro-boot`, commit `1cb042921630b7d2f6613c790ee546cf4c26c560`, firmware 1.3.155. Actual comparison is diverged, 219 ahead / 31 behind master; merge base `21ce3b5b720e106815c8f3a7778bc7003294e0e2`. Master does not contain this integration. PR348 is closed/unmerged; its head `108daf2050065076346ae016b3b030b1e7ec3e2a` is ancestral to PR350, 1,026 ahead / zero behind.

The original `usb-hid-keyboard` source/package is 0.1.1. The authentic current USB package contains 8,444 ELF bytes with SHA256 `4c9f55b63b89c7464d386401acd955df6ea3d96fb58933c13c7f1fdabe02691e`. Its `.rte.zip` SHA256 is `a9969a6a2f633972fca9fcf05af5225f8cf1eb37147dd48d60a9a7228d15f87b`. This package is part of the exact-source fixture from USB artifact 11395307892 and T5 artifact 11394988152, already durably recorded in [the authentic package manifest](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/c314652a5df0f1dbce3c868d1791d4488ac37afb/test/installed_provider_registration/fixtures/manifest.json). All seven fixture source files were independently compared with their remote Git blob IDs before reuse.

The existing 19-provider navigation directory/module-capacity repair remains exclusively with `trace_basic_reader_latency`, [claim6012329134](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6012329134), firmware 1.3.157, [checkpoint6013753617](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/332#issuecomment-6013753617). This scanner did not edit or take that work. FTDI reload, direct-open, bootstrap waits, empty-allocation cleanup and pathname reports remain separate existing work. No image/model/EPUB parser investigation was undertaken.

## Trigger, root cause and production route

Trigger: an enumerated USB boot-keyboard interface remains attached, can be claimed and safely released, but its SET_PROTOCOL request repeatedly completes unsuccessfully, for example with a completed STALL/error. The configuration descriptor is a normal, well-formed boot-keyboard interface. There is no malformed package, oversized directory or invented large dependency graph in this workload.

[`usb_hid_keyboard::poll`](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1cb042921630b7d2f6613c790ee546cf4c26c560/Drivers/usb_hid_keyboard/driver.c#L93-L130) scans interfaces, opens each unbound boot-keyboard interface and calls `set_boot_protocol`. On failure it closes and continues without recording an attempt, deadline or backoff. Because the keyboard slot is populated only after success, the same attached interface is still unknown on the next poll and repeats the complete claim/protocol/release sequence. The `max_reports` argument limits report reads, not these startup attempts. A failed protocol setup still returns a successful poll when no other error occurs.

The real [`usb_hid::set_boot`](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1cb042921630b7d2f6613c790ee546cf4c26c560/Drivers/usb_hid/driver.c#L202-L211) requests a 100 ms control deadline. The host validates the live interface claim and passes that deadline to the controller. The real controller `control`, `wait_completion`, `idle_transfer` and completion callback were exercised: a completed error returns DMA ownership and allows safe claim release, so the next foreground poll can repeat it. A deadline is a maximum requested wait, **not proof every request waits 100 ms**.

Two real callers expose the repeated work:

- Text Editor 0.2.3 declares optional `usb.hid.keyboard`, acquires it at startup and calls production `collect_keyboard` each editing loop and during serviced refresh. The one-second app retry only protects capability acquisition; after the capability exists, this internal interface-protocol retry bypasses that interval. The direct Text Editor consumer is included in the host reproduction.
- Once navigation is admitted, `MappedInputManager::update` calls `NativeNavigationInput::nativeNavigationTick` before `nativeTouchTick`. `usb-ui-navigation` calls semantic text → HID keyboard → HID → USB host synchronously. `NativeNavigationInput` retains its API and has a 20 ms poll interval, but it records that timestamp **before** the synchronous call; a completed 60 ms error already consumes that interval. The resulting repeated owner work delays touch consumption and subsequent activity dispatch. Independent touch capture can remain healthy while owner consumption is delayed.

The second route is conditional on successful navigation admission. The current authentic19-provider closure has the separately owned admission blocker; this report does not pretend that baseline closure already works. The first route is independently reachable on the original source, as verified next. Disabling external navigation alone does not prevent Text Editor's direct keyboard polling.

## Original-source admission witness

`keyboard_admission_witness.py` makes an isolated copy of the published fixture at `c314652a`, adds only a host test case, and compiles the unmodified PR350 source. It verifies all 86 supplied files / 19 authentic packages, then uses production registration, graph/module/executor/bootstrap, resolver, package inspection, HAL and shared FatFs volume. It stops before target ELF activation; physical USB startup and execution of Xtensa binaries are not claimed.

With the ordinary eight T5 bootstrap packages:

| Requested order | Registered modules | Peak directory cursors | Directory failures |
|---|---:|---:|---:|
| Direct `usb.hid.keyboard` |14|7|0|
| Touch after that keyboard closure |15|2|0|
| Touch first, then direct keyboard |15|7|0|

The keyboard closure adds t5s3-usb-power-profile, board-power-t5s3-v2, usb-controller-esp32s3, usb-host-v2, usb-hid and usb-hid-keyboard. It fits the original 16-module/eight-directory bounds. Normal and fatal ASan/UBSan runs agree and leave zero directory handles and package pins after checked cleanup. This is direct dependency-registration evidence, not merely a topological/declaration inference or a hardware activation test.

## Deterministic polling evidence

`keyboard_retry_scan.py` builds five unchanged production C drivers: USB host, HID, HID keyboard, semantic text and UI navigation. It also compiles the complete production NativeNavigationInput translation unit, the unchanged MappedInputManager update body, the actual Text Editor keyboard collector, and unchanged controller control/wait/completion bodies. IDF event delivery, clock, physical device/configuration, claim/release and interrupt endpoints are fixtures. Gamepad endpoints are empty fixtures, and unrelated serial discovery is excluded so the known FTDI path cannot contaminate the counts. Package admission is established separately above; the polling test supplies an already-admitted API.

There are 1,000 observed polling turns after the navigation subscription's initial neutral rearm. Delayed completed failures inject a callback at 60 artificial milliseconds, inside the real 100 ms control budget. The zero-time failure control establishes the same retry count without injecting any latency.

| Case | SET_PROTOCOL calls | Physical claims / releases during observation | Injected wait before each following touch-service marker |
|---|---:|---:|---:|
| No keyboard attached |0|0 /0|0|
| Healthy keyboard |1|1 /0|0|
| One completed failure, then success |2|2 /1|60ms once|
| Persistent immediate completed failure |1,000|1,000 /1,000|0|
| Persistent delayed completed failure |1,000|1,000 /1,000|60ms each|
| Two persistently failing keyboards |2,000|2,000 /2,000|120ms each|
| Failing keyboard plus healthy keyboard |1,001|1,001 /1,000|60ms each|
| Recovery after100 completed failures |101|101 /100|60ms for the first100|
| Healthy detach/reconnect |2|2 /1|0|
| Undrained control timeout |1|1 /0|100ms once, then claim quarantine|
| Actual Text Editor collector, repeated completed failure |1,000|1,000 /1,000|60ms per collector return|

The healthy second keyboard still delivers its Enter press through the real text/navigation providers, but only after the first keyboard's failing startup work on each turn. Subsequent successful admission stops the protocol requests. Disconnect/reconnect admits the new generation normally. Final checked teardown releases every remaining claim and subscription.

The timeout negative control is important: a request that never returns its IDF completion leaves `inFlight` true. The real host retains a closing claim, so the next keyboard claim is refused and **does not produce repeated control requests**. The reported repeated-latency path specifically requires completed failures with safe cleanup. It must not be generalized to every USB timeout.

Normal and fatal ASan/UBSan outputs match exactly in all 11 cases. The synthetic accumulated 60,000/120,000ms across 1,000 turns is not a single minute-long stall, observed device latency, promised improvement or estimate of this user's hardware. UI/touch counters are continuation markers; the test does not render a physical panel or assert actual tap-delivery timing. The source-qualified impact is repeated synchronous failure work on a latency-sensitive owner path for as long as the interface remains failing.

## Deduplication and nearby existing reports

Fresh census: 394 branches, 436 all-state PRs, 588 distinct heads, 194 PR332 comments through 6013753617, zero non-PR issues. All selected source objects were available. The257 historical report/ledger/README blobs were available and searched, alongside fresh canonical bugs/progress/workflow from ledger `101e2f7f58a8daabbccd3227ad878105e3847320` and current queue reports.

There are only two keyboard-driver variants across the source-bearing heads; their difference is burst report draining. The failed SET_PROTOCOL branch is unchanged in both and has no attempted-interface/backoff state. The accompanying dedup summary records source variants and counts. No current branch/PR repair for this failure was found.

[Merged PR303](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/303) fixes the analogous **gamepad report-descriptor** discovery problem. Its changed paths were checked: it changes `usb_hid_gamepad`, its package builder/tests/docs, and does not change `usb_hid_keyboard`. It is relevant precedent, not a fix for this branch. Canonical254 concerns repeated USB serial class initialization for unbindable devices; the FTDI report concerns incompatible provider interface shape and repeated ELF lifecycle. Neither is this attached keyboard's SET_PROTOCOL retry. PR216 fixes display/I2C lock duration and likewise does not bound keyboard admission retries.

Existing USB/HID aggregate passes on the exact source, covering keyboard/gamepad subscriptions, descriptors, ordered typing, disconnect/cleanup, controller interrupt persistence, role switching, navigation focus and firmware input lifetime. These are adjacent negative controls, not proof this fault was previously covered.

## Repair direction and boundaries

If selected for implementation, add bounded per-attachment/device/interface/alternate keyboard-admission retry state, with elapsed-time backoff, a finite attempt/work budget, throttled diagnostics, and at most a bounded amount of startup work per owner turn. Reuse the established class-discovery approach where appropriate, while explicitly preserving retry after transient recovery and a defined rearm event. Do not silently infer that a previously failing protocol can never recover during that attachment.

Keep normal accepted-keyboard report draining, current host claim authorization, ordered key events, separate healthy keyboard progress, reconnect generation handling and failed-cleanup quarantine. Never skip SET_PROTOCOL safety, reset the USB controller from the app, drop an in-flight claim, or force-unload a provider to improve timing. A future driver source change must increment `usb-hid-keyboard`'s own package version. No such change is part of this report.

## Reproduce and limitations

Use an unchanged checkout at 1cb04292, C/C++ compilers and Python3:

```sh
python3 keyboard_retry_scan.py SOURCE_CHECKOUT OUTPUT_DIRECTORY --sanitize
```

For the independent direct-registration witness, provide a second checkout containing the exact published test fixture at c314652a and an already installed ArduinoJson 7.4.2 include directory:

```sh
python3 keyboard_admission_witness.py FIXTURE_CHECKOUT SOURCE_CHECKOUT ARDUINOJSON_SRC OUTPUT_DIRECTORY --sanitize
```

Both scripts write only their supplied output directories. The original fixture's GCC `-Wno-error=maybe-uninitialized` accommodation is retained; sanitizer compilation emits the existing ArduinoJson warnings. The host-only C++ inclusion of the unchanged C Text Editor collector accepts its standard `{0}` initialization via `-Wno-missing-field-initializers`; the initial strict C++ warning was a fixture compile issue, not a production failure. LeakSanitizer is disabled under tracing. No target build, device measurement, USB operation, installation, implementation/master/PR350/shared-ledger write, merge, release or new repair PR was performed. Earlier endpoint-only exploratory results that did not distinguish completed failure from an undrained timeout are superseded by the final controller-body tests above.

The existing PR332 single writer retains canonical reconciliation ownership. This isolated immutable checkpoint is a queue entry for that coordinator; it does not increase canonical counts itself or steal any active claim.
