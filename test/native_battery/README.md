# Installed battery consumer host regression

Run `test/run_native_battery_gauge_test.sh` from the repository. It compiles the
real consumer and X4 Board adapters against the real graph/SDK declarations,
with fake owner-task, monotonic clock and provider operations. ASan and UBSan
are enabled. In ptrace-based executors where LeakSanitizer cannot run, use
`ASAN_OPTIONS=detect_leaks=0 test/run_native_battery_gauge_test.sh`.

Coverage: unavailable startup/cache-only Board access; valid zero and full
percentages without inferred full/VBUS; 5-second polling; failed and invalid
samples; 15-second expiry across millis rollover; 30-second acquisition and
release retry cadence; exact retained partial grants and failed releases;
version/size/function/interface/grant validation and recovery; concurrent
render readers; wrong-owner and reentrant tick rejection; callbacks outside
the snapshot lock; grantless failed starts. A source-level order guard checks
that setup primes the optional cache after storage/input and before Home.
A separate non-X4 executable proves the new consumer does
not link Arduino, RTOS or graph dependencies into the legacy path.

The test does not emulate the installed provider graph's SD admission or prove
physical battery readings. The consumer leaves grantless failed activations
under graph ownership, never shuts down the shared graph, and retains its
successful lease until reboot. No X4 sleep support is implied. Provider calls
have duration determined by their implementation; the cache cannot enforce a
timeout or preempt a broken callback. Periodic acquisition can recover from
absence or a failed start whose cleanup succeeded. The graph rejects a node
whose failed start could not quiesce, keeping it unavailable and pinned until
its lifecycle owner explicitly recovers it or the device reboots. Retrying
acquisition alone does not clear this quarantine.
