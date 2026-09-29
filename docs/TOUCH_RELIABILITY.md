# Touch reliability: enforced contracts and regression coverage

This change addresses demonstrated software defects, not a claim that a hardware
trace has identified every missed tap on the owner's device. Firmware 1.3.38,
`gt911-touch` 0.1.1 and `i2c-esp32s3-v2` 0.1.6 form the complete fix. Install all
three after release; a firmware-only update cannot replace installed driver code.
The stable capability IDs and API-v1 layouts remain unchanged.

## Why earlier fixes were insufficient

The history separated fixes that actually depend on one another:

- `5490d759` moved capture off the UI/render loop. That protects against slow
  rendering, but the new worker still calls the same shared bus and raw provider.
- `4f25762b` / `a07e730c` replaced busy rejection with I2C mutex queueing. That
  protects individual bus transfers, not the whole touch status/read/ack cycle.
- `3ca24602` tolerated two failed polls, but the third reapplied a cached snapshot
  and made a previously observed DOWN ineligible. Failure count is neither an
  elapsed-time deadline nor evidence of lost event history.
- `gt911-touch` 0.1.0 acknowledged READY after a failed coordinate read, discarding
  data that had never been delivered. Firmware also returned before draining
  valid queued events when a later operation in a polling batch failed.
- The public touch header claimed executor serialization, but interface pointers
  are directly callable and the driver had no report/state mutex. Independent
  calls could interleave status, coordinates, acknowledgements and queues.
- The I2C provider gave its backend a fresh timeout after waiting for its own
  mutex; the backend then waited on the board mutex with `portMAX_DELAY`.
  Therefore the 20 ms touch timeout did not bound admission to physical I/O.
- Tests mostly checked source shapes and successful sequential reports. The I2C
  host mutex treated **every** nonzero timeout as an infinite wait. Those tests
  could not enforce the missing timing and concurrency guarantees.

## Contracts implemented here

1. The installed GT911 provider owns the entire status/read/ack/state operation
   under one ordinary FreeRTOS mutex. Subscribe, unsubscribe, next and snapshot
   use the same mutex. No relocatable atomics or hardware imports are added.
   Start/stop remain serialized by the loader, with live grants pinning the ELF.
2. A failed coordinate read leaves READY untouched for the next poll. Malformed
   reports that are discarded mark every subscriber GAP. Failed acknowledgement
   does not publish a report. Subscription tokens remain monotonic across restart.
3. Poll services at most one report: three bus transfers at most, each with a
   20 ms budget, plus bounded provider admission. Firmware yields at least one
   scheduler tick between capture turns. UI redraws never become the sampler.
4. Bus-provider queue wait is subtracted before calling the transport. Physical
   board-lock wait is bounded and subtracted again before setting the Wire
   transfer timeout. No transfer starts after the admission budget expires.
   Tick rounding and scheduling can add latency; this is not a hard real-time
   guarantee against a broken lower-level hardware implementation.
5. Firmware drains events even after a failed poll. `next == -2` means temporary
   fault/busy, not proven GAP. Brief interruptions preserve a known gesture;
   a continuous one-second outage cancels unfinished held state once, without
   synthesizing a release or deleting completed taps. Explicit GAP still
   resnapshots immediately.
6. Snapshot sequence is a replay boundary: events already represented by it are
   ignored, including events queued by another caller between GAP and snapshot.
7. Capture records copied counters only. The UI owner emits a bounded-rate
   `TOUCH` log summary after faults (at most once per five seconds): polls,
   failures, gaps, events, completed taps, tap-queue overflow, cancelled outages,
   maximum service duration and maximum interval between capture calls. No log
   or SD write is introduced in the sampling task.

The existing 16-tap queue remains bounded. It tolerates a temporarily busy UI;
indefinite UI stalls or overflowing it are now observable, not silently treated
as successful input. A bus timeout does not reset the shared bus, touch controller
or display, and does not unload a live provider.

## Executable protection

Run:

```sh
bash test/run_gt911_touch_test.sh
bash test/run_i2c_esp32s3_v2_test.sh
bash test/run_idle_power_test.sh
```

Coverage includes actual driver fanout and report retries, failed acknowledgements,
malformed-report GAP, concurrent polling/snapshot attempts, stale tokens after
restart, finite mutex waits, remaining-budget forwarding, board admission timeout,
Wire timeout restoration, timestamp rollover, consumer transient/outage/GAP
recovery, snapshot replay races and swipes. An integrated host fixture connects
the actual C driver to the production firmware consumer and delivers 100 taps
with injected point-read/ack failures and delayed UI consumption, asserting exact
once-only delivery. Host substitutions cover physical I2C, clock and RTOS; they
do not emulate display scan interrupts or prove physical touch behavior.

The existing firmware and provider CI runners execute these tests. The GT911
builder derives FreeRTOS configuration from the target compilation database and
validates only the four required mutex imports against the existing privileged
OS/CPU inventory. Package generation continues to derive versions and imports
from each driver, without a new catalog or normal-app hardware bypass.

## Remaining hardware check

Test navigation during repeated display refreshes, with USB host attached and
with charging/serial power, then return from GameBoy/Model Viewer and test again.
Check tap and swipe behavior in both orientations. If taps still disappear,
retain the TOUCH summary with firmware and installed-driver versions: rising
`fail` indicates provider/bus contention or I/O failure; `gaps` indicates discarded
or overflowing raw history; `overflow` identifies delayed UI consumption;
`maxService`/`maxGap` distinguish bus work from capture scheduling stalls. Electrical
faults, controller firmware behavior and physical refresh interference remain
hardware questions, not something host tests can certify away.
