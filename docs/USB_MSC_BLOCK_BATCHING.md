# MSC 0.1.7: bounded whole-sector batching

Builds on 0.1.6's checked shutdown fix. The class buffer grows from 512 to 4096
bytes, and shared callbacks forward one through eight whole sectors via the
existing generic storage export ABI. No X4 type or native SDMMC call enters MSC.
The final command chunk may be smaller than eight sectors. Nonzero byte offsets,
partial-sector lengths, invalid ranges and oversize chunks are rejected before
storage access. No read-modify-write, speculative read-ahead or write replay is
introduced. Client buffers need not be DMA-aligned; the storage backend owns that
contract. Command geometry validation remains exact.

The pinned TinyUSB adaptation rejects failed or short data completions before
calling the write callback. This also rejects an aligned short USB transfer;
accepting it would confuse a short packet with a valid final command chunk. A
normal final chunk is compared to the remaining command length and accepted.

Memory is bounded to one 4096-byte class buffer (3584 bytes more than 0.1.6),
with no added queue or retained user pointer. The existing 32-pass/2-ms owner pump
is unchanged. One in-flight synchronous storage call may exceed that deadline;
its backend must remain bounded. The selected X4 backend accepts one request of
at most eight sectors with its existing one-second command timeout, rather than
eight independent calls. Existing failure/ownership/eject/cleanup checks remain.

RAM-only diagnostics v2 already expose total/max IO milliseconds, last IO block
count/result, command elapsed times, blocks transferred, poll gaps and CSW
residue. They remain readable after eject and before consuming the session token
with end(). No synchronous transfer-mode logging or ABI change is added. The X4
production-path test checks these after teardown and uses an explicitly synthetic
10-ms batch to verify the pump returns before processing another queued event.
This does not establish hardware latency or throughput.

Tests: existing provider/owner and pump suites; X4 test_usb_bulk_profile.py links
this production source, patched TinyUSB, actual X4 SD and shared FatFs. It checks
bulk call counts, non-batch-aligned LBAs, unaligned caller buffers, final short
batches, last-sector boundaries, invalid partial sectors, short/failed USB
completion, read/write and partially accepted write failures, queued reset,
retained custody, host eject and remount. Normal and UBSan (excluding the existing
TinyUSB unaligned wire access) are the available host qualifications.
