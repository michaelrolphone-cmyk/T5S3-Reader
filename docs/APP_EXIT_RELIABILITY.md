# App exit: memory and display ownership

## Demonstrated display failure

The pinned M5GFX 0.2.20 `src/lgfx/v1/platforms/esp32/Panel_EPD.cpp`
allocates five buffers, an update queue, and a background task in
`init_intenal()`, but its `Panel_EPD` destructor is empty. The base
`Panel_HasBuffer` destructor frees only `_buf`. RiscRTE deletes this panel on
every handoff to a video app and creates another on return.

At 960 x 540, the leaked `_step_framebuf` alone is **1,036,800 bytes**.
The LUT, two DMA rows, queue, and task also survive. That task retains `this`
and its bus pointer after both objects have been destroyed. Repeated app
switching can therefore exhaust PSRAM/internal DMA memory and leave dangling
worker references. This is independent of whether Risc Strike, Model Viewer,
or Hollow Trail correctly frees its own buffers.

Source pin: https://github.com/m5stack/M5GFX/tree/0.2.20/src/lgfx/v1/platforms/esp32
The test fixtures preserve the exact upstream source and license header.

## Repair

`scripts/patch_m5gfx_lifecycle.py` patches the pinned dependency after dependency
installation and before compilation, for every T5S3 build/release environment.
It fails the build if its source anchors change. It introduces cooperative
worker shutdown, a bounded join, and idempotent release of all five buffers and
the queue. The worker completes its current scan/DMA before its final atomic
completion signal. Queue/task creation failures roll back allocations.

The host joins the panel worker **before** releasing the LCD bus or destroying
the panel. A join failure prevents handoff. Destruction without a successful
join fails closed rather than destroying memory still used by the worker.

The raw video bridge now bounds DMA/initial-flip/settling waits. Failed worker,
DMA, panel-I/O, or bus teardown preserves live allocations/handles and propagates
failure to the loader. It does not reinitialize another display owner over live
hardware. An unsafe app unload retains its image/heap and refuses the next app;
restart is required in that exceptional state. A normal exit releases resources.

## App allocation ownership

An invocation ledger starts before app relocation/constructors. Its bounded
4096-entry metadata table lives in PSRAM. Ordinary app imports for `malloc`,
`calloc`, `realloc`, `free`, exported `heap_caps_malloc/calloc/free`, and the
existing nothrow-new/delete compatibility symbols are routed through it. The
public app PSRAM allocation API uses the same ledger. Explicit free removes the
entry; failed realloc retains the original allocation. Remaining allocations
are reclaimed after app finalization/provider release and before ELF unload.
Video-app memory is reclaimed after video stop, before the host display needs
its own large working set again. Partial launch failures use the same cleanup.

Only relocation of the main application image selects these wrappers. Scoped
provider relocations and later legacy driver mappings retain their independent
allocators. Firmware allocations made inside host APIs are not indiscriminately
charged to whichever app happens to be running. Shared driver/system objects
must survive according to their own resource ownership. Ordinary allocator
calls made directly by firmware remain unchanged.

`APP_MEM` logs record outstanding block/byte counts, peak app bytes, internal
free/largest block and PSRAM, followed by post-cleanup free values through the
normal system logger. Thus app leaks and internal fragmentation can be told
apart without relying solely on a USB serial connection.

This does not introduce MMU isolation or make arbitrary unsafe native code safe.
Apps must still stop their private workers/IRQs/DMA before returning. Hardware
resources and firmware-owned API objects require their own lifecycle cleanup;
they cannot safely be reclaimed by scanning the global heap.

## Verification

- Actual patched upstream allocator/startup/shutdown methods: 100 lifetimes,
  all five allocation failures, queue failure, task failure, timeout retention,
  repeated cleanup and patch idempotence/source-drift rejection.
- Invocation ledger: 100 lifetimes, forgotten allocations, capacity exhaustion,
  calloc overflow/zeroing, realloc failure and double-cleanup.
- Actual firmware memory bridge: main-image-only relocation, another task's
  lookup, later driver mapping, existing foreign-pointer free compatibility,
  repeated reclaim and closed-lifetime allocation denial.
- Actual raw-video teardown: worker/DMA timeout, I/O/bus deletion failure,
  retained buffers, retry and repeated stop.
- Launcher regression: cleanup before unload, partial startup, finalizers,
  failed restore, retained-live-hardware refusal of subsequent launch.
- Full native-app regression suite.

Host tests replace RTOS/I2C/LCD operations. Target CI checks real headers/linking;
physical launch/exit cycling is still required to establish hardware behavior.
Firmware 1.3.38 includes these changes in the same unreleased PR as the touch
repair; no app package bytes are changed by this lifecycle fix.
