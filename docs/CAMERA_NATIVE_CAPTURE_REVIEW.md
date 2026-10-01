# Native camera capture comparison

Reference: Espressif esp32-camera v2.0.4, e689c3b082985ee7b90198be32d330ce51ac5367,
`target/esp32s3/ll_cam.c` and `driver/cam_hal.c`. This comparison precedes the
complete 0.1.4 candidate hardware run; it is not a hardware success claim.

| Transition | Reference | Installed finite polled receiver |
|---|---|---|
| Sensor initialization | OV3660 reset, JPEG, SVGA, quality | Retained sensor implementation; native private SCCB; bounded startup |
| Waiting for boundary | CAM running, stop-on-full disabled | Same; DMA parked and cannot write memory |
| Arming | CAM stop/reset, FIFO reset, GDMA RX reset, descriptor address/start, CAM update/start | Same ordering; finite 96 KiB chain rather than circular ping-pong |
| Resynchronization | `ll_cam_do_vsync` reverses matrix inversion for 10 us | Same, using bounded CPU cycle delay and existing ROM matrix import |
| DMA completion | Byte-count EOF, not VSYNC-generated DMA EOF | 1024-byte EOF; inspect only CPU-owned completed descriptors after barrier |
| Poll latency | Interrupt/event task starts near VSYNC | Discard partial prefix; require complete SOI/EOI within finite buffer; no assumption of ISR latency |
| Exhaustion | Reuses circular buffers | Never reuses DMA descriptors; stops/fails boundedly when no complete frame fits |
| Payload | Copies completed DMA chunks and verifies JPEG markers | Scan at most 512 bytes/tick; publish immutable frame only after DMA park |
| Backpressure | Frame queue | Existing 1024-byte stream, max 512-byte production/tick, absolute deadline |
| Revocation | Stops DMA before destroying callback resources | No module ISR/task; bounded 25 ms quiesce with real yields, retain all memory on uncertain park |

Intentional differences: descriptor ownership checks stay enabled because this
chain is finite; no PSRAM DMA or cache-maintenance dependence; no firmware camera
proxy or new CPU imports. `in_dscr_empty` at the end of a finite chain does not
invalidate its already committed data. Descriptor errors still fail closed.

Software tests cover split markers, discarded prefixes, truncation, scan bounds,
invalid backend lengths, stream/cancel/revocation failures and quarantine. The
lab consumer performs two complete captures separated by normal graph teardown
and restart, including active-job revocation and explicit cancel/release. Both
shutdowns require zero storage handles and grants. Private USB image bytes are
saved only in local lab evidence, verified by digest and decoded locally; they
must not be committed or uploaded to GitHub, including Actions artifacts.
