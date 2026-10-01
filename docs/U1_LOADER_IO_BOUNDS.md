# U1 loader I/O bounds

The existing application loader now refuses descriptor-table contention after
1000ms. Each VFS operation checks ownership before touching a slot; only a
successful lock acquisition is released. A close timeout retains the live slot
and reports ETIMEDOUT, so owned-buffer admission cannot mistake it for a completed
close. Four occupied slots still produce EMFILE. An abandoned failed-close slot
is intentionally unavailable rather than silently reused; this is bounded
capacity loss, not a safe force-close/recovery claim.

The existing ELF reader rejects sizes outside 52 bytes–8MiB before allocation,
even when used through a non-SD port. It requests at most 4KiB per read, checks a
30-second monotonic read budget before and after requests, yields a scheduler
tick after every successful chunk, and reports debug progress at completion or
256KiB/500ms checkpoints. Short/error reads and expired work release the owned
buffer and attempt the existing descriptor cleanup. Admission still follows a
successful source close and uses the exact copied bytes.

## Executed tests

- `sd_vfs_lock_test.py` compiles the actual production SdVfs.cpp with deterministic
  semaphore/storage adapters. Every failed lock returns ETIMEDOUT without a file
  access or unmatched unlock. Successful reads/seeks/stat, four-slot exhaustion,
  failed-close retention, successful later close, read-only and invalid-FD rules
  are exercised
- `elf_owned_admission_test.py` compiles the actual production reader and weak
  fallback. It checks the strong owned-buffer callback, close ordering, cleanup,
  4096/4096/1-byte requests, scheduler yields, deadline expiration, counter wrap,
  preallocation limits and negative/empty reads
- Both fixtures are connected to the existing native-app host aggregate, which
  passed with this source

## Explicit remaining boundary

These changes bound descriptor contention and repeated loader work. They cannot
interrupt a synchronous media call which itself fails to return. HalStorage's
shared storage mutex still uses portMAX_DELAY, including file destruction and
move assignment, and board/SdFat/network lower-I/O termination remains open.
Changing that mutex to a timed wait without an ownership-safe destructor and
uncertainty contract would permit unlocked FsFile destruction or lose tracked
handles. No such unsafe substitution is made. No new bus driver, registry or
forced unmapping is introduced. This slice therefore does not close the whole
bounded-I/O acceptance requirement. Target verification is pending.

**Implementation In Progress**
