# T5S3 Tiered Memory and Storage-Backed Virtual Memory Architecture

## Status

Architecture specification for memory management across internal SRAM, PSRAM, and removable SD storage in the T5S3 application/runtime framework.

This specification defines a **tiered working-set memory model**. It deliberately does not attempt to emulate conventional demand-paged virtual memory on hardware without a general-purpose process MMU/page-fault mechanism.

> **Core invariant:** Executable code, stacks, and actively dereferenced data reside in addressable RAM. Large, cold, durable, and reconstructable data may reside on SD and enter RAM only as bounded mapped working sets.

---

## 1. Goals

The memory architecture SHALL:

1. preserve scarce internal SRAM for latency-sensitive system state, stacks, DMA/driver requirements, and critical runtime allocations;
2. use PSRAM for executable ELF mappings and larger active working sets;
3. allow applications and framework services to operate on datasets much larger than available PSRAM;
4. use SD storage as explicit backing storage for pageable objects, inactive state, resources, documents, caches, and application data;
5. avoid pretending that arbitrary SD bytes are directly CPU-addressable RAM;
6. expose stable storage handles rather than long-lived pointers for storage-backed objects;
7. bound the amount of PSRAM consumed by storage-backed mappings;
8. support eviction and prefetch policies appropriate to an e-paper device;
9. integrate with scene suspension/restoration and ELF unloading;
10. allow the framework to reclaim application working sets under memory pressure;
11. distinguish durable data from disposable/reconstructable cache data;
12. avoid uncontrolled SD write amplification;
13. remain usable by independently loaded ELF applications through a versioned ABI;
14. support future process/application memory arenas and resource accounting.

---

## 2. Non-goals

The initial implementation SHALL NOT claim to provide:

- transparent POSIX-style swap;
- arbitrary pointer dereference directly into SD storage;
- hardware page faults that automatically fetch missing SD pages;
- per-process virtual address spaces equivalent to a desktop MMU;
- automatic paging of arbitrary ELF stack, `.data`, `.bss`, or anonymous heap memory;
- process isolation merely because objects occupy different PSRAM allocations.

These distinctions are architectural requirements, not implementation shortcomings.

---

## 3. Memory tiers

T5 defines three primary memory tiers:

```text
                   FAST / SCARCE
                        ^
                        |
+------------------------------------------------+
| Tier 0: Internal SRAM                          |
|                                                |
| system stacks                                  |
| scheduler/runtime state                        |
| interrupt-critical state                       |
| DMA/internal-memory-required buffers           |
| hot framework objects                          |
| small latency-sensitive allocations            |
+------------------------------------------------+
                        |
+------------------------------------------------+
| Tier 1: PSRAM                                  |
|                                                |
| executable ELF mappings                        |
| active application/scene working sets          |
| UI object trees                                |
| decoded resources                              |
| storage-VM page/window cache                    |
| large non-critical resident buffers             |
+------------------------------------------------+
                        |
+------------------------------------------------+
| Tier 2: SD-backed storage                      |
|                                                |
| inactive scene state                           |
| application model data                         |
| document/book contents                         |
| images/resources                               |
| large collections/databases                    |
| network/download caches                        |
| inactive application data                      |
| storage-backed virtual objects                 |
+------------------------------------------------+
                        |
                        v
                   SLOW / LARGE
```

The framework SHALL choose tiers according to access requirements rather than treating all memory as interchangeable.

---

## 4. Fundamental distinction: resident memory versus virtual objects

T5 SHALL expose two conceptually different allocation classes.

### 4.1 Resident memory

Resident memory produces an ordinary pointer and remains CPU-addressable for the lifetime of the allocation.

Conceptually:

```c
void *t5_malloc(size_t size);
void  t5_free(void *ptr);
```

Resident allocations may be satisfied from internal SRAM or PSRAM according to allocation policy and capability requirements.

Resident memory is appropriate for:

- stacks;
- ELF `.data` and `.bss`;
- active object graphs;
- small frequently accessed structures;
- buffers requiring continuous pointer access;
- active computation.

### 4.2 Storage-backed virtual objects

Storage-backed allocations return handles, not permanent pointers.

Conceptually:

```c
t5_vm_handle_t t5_vm_open(const char *path, uint32_t flags);
t5_vm_handle_t t5_vm_create(const char *path, uint64_t size, uint32_t flags);
```

A portion of the object becomes addressable only while explicitly mapped:

```c
t5_vm_view_t view = t5_vm_map(handle, offset, length, T5_VM_READ);

process(view.ptr, view.length);

t5_vm_unmap(&view);
```

The pointer returned by `t5_vm_map()` is temporary and SHALL NOT remain valid after unmap, object close, application teardown, or forced revocation according to the API contract.

---

## 5. Why SD is not ordinary RAM

An ELF instruction performing:

```c
x = object->field;
```

requires `object` to reference currently addressable memory.

The initial T5 architecture SHALL NOT attempt to intercept arbitrary missing-memory accesses and synchronously fetch SD sectors as a hardware page-fault handler would on a conventional MMU system.

Instead:

```text
storage handle
      |
      v
t5_vm_map()
      |
      +---- cache hit ------> existing PSRAM window
      |
      +---- cache miss
                |
                v
           allocate cache page(s)
                |
                v
              SD read
                |
                v
             PSRAM
                |
                v
       return temporary pointer
```

This makes I/O latency and pointer lifetime explicit and controllable.

---

## 6. Logical memory capacity

Storage-backed virtual objects allow logical datasets to be much larger than physical RAM.

For example:

```text
Application logical dataset

500 MB catalog/database/document
                |
                v
             SD card
                |
          active window only
                |
                v
         64-256 KB PSRAM
                |
                v
          application/UI
```

The framework therefore distinguishes:

```text
logical object size != resident working-set size
```

An application may operate on hundreds of megabytes or gigabytes of logical content while maintaining only a bounded resident window.

---

## 7. Storage VM object model

A virtual object is a framework-managed storage object identified by an opaque handle.

Conceptually:

```c
typedef uint32_t t5_vm_handle_t;

typedef struct {
    void    *ptr;
    size_t   length;
    uint64_t offset;
    uint32_t flags;
    uint32_t token;
} t5_vm_view_t;
```

The exact ABI SHALL be versioned before implementation.

Virtual objects SHOULD expose metadata including:

- object size;
- access mode;
- durability class;
- cacheability;
- preferred access pattern;
- owner application/system service;
- dirty state;
- backing path/object identifier;
- optional content/version identity.

Opaque handles SHALL be preferred over exposing internal filesystem or cache structures to application ELFs.

---

## 8. Mapping semantics

`t5_vm_map()` SHALL request a bounded contiguous logical range.

Conceptual signature:

```c
int t5_vm_map(
    t5_vm_handle_t handle,
    uint64_t offset,
    size_t length,
    uint32_t flags,
    t5_vm_view_t *out_view
);
```

The framework SHALL validate:

- handle ownership;
- offset and length overflow;
- object bounds;
- access permissions;
- maximum mapping size;
- current memory budget;
- requested read/write mode.

A successful view SHALL reference resident RAM, normally PSRAM.

Mapping SHALL NOT imply that the entire virtual object becomes resident.

---

## 9. Unmapping semantics

`t5_vm_unmap()` releases the application's pin on a mapped view.

After the final pin/reference is released, cached pages become eligible for eviction.

For writable mappings, unmap MAY:

- mark pages dirty and defer writeback;
- synchronously write through;
- commit transactionally;
- reject writeback when the backing medium is unavailable.

The durability behavior SHALL be selected explicitly by object/mapping policy.

Applications SHALL NOT assume that unmapping a dirty cache immediately commits durable data unless the selected API guarantees it.

---

## 10. Page/window cache

The implementation SHOULD maintain a PSRAM-backed cache for virtual-object data.

A conceptual cache entry is:

```c
typedef struct {
    t5_vm_handle_t object;
    uint64_t       page_index;
    void          *resident_ptr;
    uint32_t       pin_count;
    uint32_t       flags;
    uint64_t       last_access;
} t5_vm_page_t;
```

Relevant flags may include:

```text
VALID
DIRTY
PINNED
PREFETCHED
DISCARDABLE
WRITEBACK_PENDING
```

The cache SHALL have an explicit maximum budget and SHALL NOT consume arbitrary PSRAM until allocation failure.

---

## 11. Page size

The first implementation SHOULD use a fixed internal cache granularity selected through measurement rather than assuming desktop page sizes are optimal.

A 4 KiB class page is a reasonable initial benchmark, but SD block behavior, FAT/filesystem overhead, PSRAM allocation overhead, access locality, and application workloads SHALL be measured.

The public ABI SHOULD describe offsets and lengths rather than expose cache page size so that the implementation can change later.

Large sequential mappings MAY use larger read windows than the underlying cache granularity.

---

## 12. Eviction policy

Unpinned clean pages may be discarded immediately.

Dirty pages require writeback or explicit discard semantics before reuse.

The initial eviction policy SHOULD be simple and bounded, such as LRU approximation or CLOCK.

Eviction priority SHOULD account for semantic class where known:

```text
highest eviction preference

prefetched-but-unused data
reconstructable network/resource cache
inactive list/database windows
inactive document neighbors
active-but-unpinned object pages

lowest eviction preference
```

Pinned mappings SHALL NOT be evicted while their pointer contract remains valid.

The framework SHALL detect excessive pinning that prevents memory reclamation.

---

## 13. Prefetch

The storage VM SHOULD support asynchronous or advisory prefetch.

Conceptually:

```c
int t5_vm_prefetch(
    t5_vm_handle_t handle,
    uint64_t offset,
    size_t length
);
```

Prefetch SHALL NOT pin data indefinitely and SHALL yield to demand mappings under memory pressure.

Useful patterns include:

```text
Document reader

visible:   page 174
resident:  173, 174, 175
prefetch:  176
```

and:

```text
Virtual list

visible rows: 350-365
resident rows: 320-400
prefetch direction follows scrolling
```

E-paper refresh latency provides opportunities to perform SD reads while the display is updating or otherwise not requiring immediate CPU-driven presentation work.

---

## 14. Access-pattern hints

The API MAY accept advisory access patterns:

```text
T5_VM_ACCESS_RANDOM
T5_VM_ACCESS_SEQUENTIAL
T5_VM_ACCESS_FORWARD
T5_VM_ACCESS_REVERSE
T5_VM_ACCESS_ONCE
T5_VM_ACCESS_HOT
```

Hints affect caching and prefetch only and SHALL NOT alter correctness.

For example, sequential access can use larger read-ahead windows while random database access should avoid polluting the cache with unnecessary neighbors.

---

## 15. Durability classes

Virtual objects SHOULD declare their durability semantics.

### Durable

User/application data that must survive normal restart and eviction.

Examples:

- annotations;
- settings;
- application databases;
- explicitly persisted scene/application model state.

### Reconstructable

Data that can be regenerated or downloaded again.

Examples:

- decoded image cache;
- generated thumbnails;
- network catalog cache;
- rendered document page cache.

### Ephemeral

Storage-backed scratch space whose contents need not survive clean shutdown or restart.

Examples:

- large sort workspace;
- temporary conversion output;
- intermediate indexes.

The eviction/writeback policy SHALL respect durability class.

---

## 16. Write policy and SD endurance

SD-backed memory SHALL NOT blindly emulate write-heavy RAM.

The framework SHOULD reduce write amplification through:

- dirty-page coalescing;
- bounded delayed writeback;
- append/journal patterns for durable metadata where appropriate;
- avoiding repeated persistence of rapidly changing transient state;
- reconstructable caches rather than durable writes when possible;
- explicit flush/commit points.

Applications SHALL NOT use storage VM as a substitute for high-frequency scratch RAM when the workload would continuously rewrite SD sectors.

---

## 17. Flush and commit

The API SHOULD distinguish cache release from durability.

Conceptually:

```c
int t5_vm_flush(t5_vm_handle_t handle);
int t5_vm_sync(t5_vm_handle_t handle);
```

Exact semantics SHALL be specified with the ABI. A likely distinction is:

- `flush`: schedule/write dirty cache toward backing storage;
- `sync`: do not return success until required durable writes are complete.

Transactional application data SHOULD use a higher-level database/journal service rather than assuming arbitrary mapped writes are crash-atomic.

---

## 18. Failure semantics

SD storage is removable and fallible.

The storage VM SHALL explicitly handle:

- SD removal;
- mount failure;
- read error;
- write error;
- short read/write;
- filesystem full;
- corrupted backing file;
- stale handles;
- unavailable durable writeback.

A mapping failure SHALL return an error rather than an invalid pointer.

If the SD card disappears while a clean resident view is pinned, the framework MAY allow that existing view to remain readable until unmap. New misses cannot be satisfied.

Dirty durable data whose backing store disappears SHALL be reported as unsynced/error state and SHALL NOT be silently discarded.

---

## 19. Application ownership and quotas

Every virtual object and mapping SHALL have an owner.

Conceptually:

```text
T5 application execution context

app_id
resident memory budget
storage-VM cache budget
open virtual objects
mapped/pinned bytes
dirty bytes
file handles
capabilities
```

The framework SHOULD enforce per-application limits for:

- maximum simultaneously mapped bytes;
- maximum pinned pages;
- maximum open virtual objects;
- maximum dirty/writeback backlog;
- optional durable storage quota.

System services MAY have separate reserved budgets.

One application SHALL NOT be able to exhaust all PSRAM by indefinitely pinning storage-backed views.

---

## 20. Pointer rules for ELF applications

ELF applications SHALL treat pointers returned from storage VM as borrowed views.

Rules:

1. a mapped pointer is valid only for its documented mapping lifetime;
2. it SHALL NOT be persisted in application state;
3. it SHALL NOT be used after `t5_vm_unmap()`;
4. it SHALL NOT be assumed stable across scene suspension;
5. it SHALL NOT be passed asynchronously unless the mapping remains pinned by an explicit supported mechanism;
6. storage handles/offsets SHALL be persisted instead of mapped pointers;
7. the framework may invalidate all mappings during application teardown.

This makes scene/ELF eviction compatible with storage-backed data.

---

## 21. ELF memory model

Normal ELF memory remains resident while the ELF is loaded:

```text
ELF
├── executable mapping      -> addressable RAM/PSRAM
├── .data                   -> resident RAM
├── .bss                    -> resident RAM
├── task stack              -> resident RAM
├── ordinary heap           -> resident RAM/PSRAM
└── storage VM handles      -> SD-backed logical objects
```

The first implementation SHALL NOT transparently page `.data`, `.bss`, stacks, or arbitrary heap allocations to SD.

Applications that need large memory SHOULD redesign large datasets as virtual objects rather than request enormous anonymous resident allocations.

---

## 22. Scene-runtime integration

The scene runtime is naturally compatible with the tiered model.

While active:

```text
NavigationPath/state
        |
        v
active scene controller ELF
        |
        v
concrete UI + working set in RAM
        |
        v
large model/resources via storage VM
```

When a scene becomes inactive under pressure:

```text
serialize restoration state
        |
        v
release UI object tree
        |
        v
unmap storage views
        |
        v
release scene resident allocations
        |
        v
optionally unload ELF
```

The NavigationPath, application model, and durable/reconstructable backing data can remain without keeping the controller or its complete dataset resident.

---

## 23. Application suspension

Application suspension SHOULD provide a deterministic reclamation point.

The runtime SHOULD be able to:

1. notify the active scene/application of impending suspension;
2. reject new long-lived mappings;
3. serialize required restoration state;
4. flush required durable virtual objects;
5. unmap/revoke application views;
6. release application cache pages;
7. destroy disposable UI/controller objects;
8. unload inactive ELF code when policy requires;
9. retain only the small state required to restore the application later.

This allows PSRAM to be reused aggressively between applications.

---

## 24. Memory-pressure levels

The framework SHOULD expose centralized memory pressure rather than forcing each app to infer heap exhaustion.

Conceptual levels:

```text
NORMAL
PRESSURE
CRITICAL
```

At `PRESSURE`, the framework may:

- discard unused prefetch;
- shrink reconstructable caches;
- evict cold clean VM pages;
- ask scenes to release optional resident data.

At `CRITICAL`, it may additionally:

- suspend inactive scenes;
- unload inactive ELFs;
- aggressively reclaim unpinned application VM cache;
- refuse large new resident allocations/mappings;
- fall back to smaller decode/render windows.

Memory pressure handling SHALL occur before uncontrolled allocation failure where practical.

---

## 25. Framework allocation policy

Framework code SHOULD express allocation intent rather than scattering direct allocator choices throughout applications.

Future APIs may distinguish:

```text
T5_MEM_INTERNAL
T5_MEM_RESIDENT
T5_MEM_LARGE
T5_MEM_DMA
T5_MEM_EXECUTABLE
T5_MEM_DISCARDABLE
```

The runtime maps these intents onto appropriate ESP-IDF memory capabilities and T5 policy.

Application ELFs SHOULD prefer T5 allocation APIs where resource accounting and future isolation are required.

---

## 26. Virtualized UI collections

Framework list/grid controls SHOULD be virtualized.

A list containing 20,000 logical rows SHALL NOT require 20,000 instantiated row controls or 20,000 resident model objects.

Conceptually:

```text
SD model: 20,000 records
          |
          v
storage VM/database window
          |
          v
resident model: nearby rows
          |
          v
framework list: visible row controls only
```

Scrolling shifts the logical/model window and reuses physical UI row objects.

This is a major consumer of the storage-backed memory architecture and SHOULD be designed into framework list APIs.

---

## 27. Document and image workloads

Document-oriented applications SHOULD retain compressed/source representation on SD and materialize only the current working set.

Example:

```text
SD
--------------------------------
source book/document
page/index data
image resources

PSRAM
--------------------------------
current decoded page
neighbor page/cache
active image decode tiles

Internal SRAM
--------------------------------
renderer state
input state
critical framework objects
```

Large images SHOULD support tiled or scanline decoding where codecs permit rather than requiring a full uncompressed image in PSRAM.

---

## 28. Executable PSRAM pressure

Because dynamically loaded ELF code consumes PSRAM/executable mapping resources, executable residency SHALL participate in global memory-pressure policy.

The runtime SHOULD treat these as separate but competing working sets:

```text
PSRAM budget

framework resident data
+ active ELF code/data
+ active UI
+ application resident heap
+ storage-VM cache
+ decoded resources
```

The storage-VM cache SHALL shrink before critical executable/application allocations fail, subject to pinned-view constraints.

Inactive ELF unloading and VM cache eviction are complementary mechanisms.

---

## 29. Cache budgeting

The storage VM SHALL have configurable minimum, target, and maximum PSRAM budgets.

Conceptually:

```text
minimum cache
    reserved for forward progress

target cache
    normal operating size

maximum cache
    opportunistic ceiling when memory is otherwise free
```

The cache may grow opportunistically when PSRAM is free and shrink when executable/UI workloads require memory.

A cache hit improves performance; cache residency SHALL NOT become a correctness requirement.

---

## 30. Zero-copy and direct I/O

Not all storage operations need storage VM.

For streaming workloads, direct bounded I/O may be more efficient:

```text
SD -> fixed PSRAM buffer -> decoder/parser -> output
```

The framework SHOULD permit direct streaming APIs alongside mapped virtual objects.

Storage VM is preferred when locality/reuse benefits from caching or when an application needs random logical access. Streaming is preferred for one-pass large content.

---

## 31. Security interaction

The storage-backed memory system SHALL follow the security architecture.

Applications SHALL access only objects permitted by their application identity/capabilities.

Opaque VM handles SHOULD be scoped to the owning execution context so one app cannot guess another application's handle and map its data.

All offset/length arithmetic crossing the ELF/framework boundary SHALL be validated.

Storage-backed executable code is outside the scope of this VM API. Executable ELF authentication, authorization, and mapping remain governed by `SECURITY_ARCHITECTURE.md` and the driver/application loader.

A writable storage VM mapping SHALL never implicitly make data executable.

---

## 32. Concurrency

The storage VM SHALL define synchronization for simultaneous mappings of the same object/range.

The initial implementation SHOULD prefer simple semantics over complex shared-memory behavior.

Possible initial rule:

- multiple read mappings allowed;
- writable mapping requires exclusive ownership of overlapping pages;
- flush/writeback serialized by the VM service;
- object close fails or defers while mappings remain pinned.

Application ELFs SHALL NOT directly manipulate cache metadata.

---

## 33. Async I/O

The synchronous `map()` API is sufficient for correctness but may block on SD misses.

The framework SHOULD eventually support asynchronous preparation:

```text
prefetch request
      |
      v
storage worker
      |
      v
SD read
      |
      v
PSRAM cache
      |
      v
event/future ready
```

UI code can request upcoming data before it is required.

Storage I/O SHOULD NOT unnecessarily block display/input-critical execution paths.

---

## 34. Statistics and diagnostics

The runtime SHOULD expose memory diagnostics including:

```text
internal SRAM free/minimum
PSRAM free/largest block
ELF resident bytes
UI/application resident bytes
VM cache current/target/max bytes
VM cache hits/misses
SD bytes read/written
prefetch hits/waste
pinned VM bytes
 dirty VM bytes
cache evictions
writeback failures
mapping latency
```

Per-application accounting SHOULD be available in development builds.

These metrics are necessary to tune page/window size, cache budget, and prefetch behavior from real workloads.

---

## 35. Performance principles

The implementation SHALL optimize for working-set locality rather than maximum theoretical virtual-object size.

Primary principles:

1. keep hot metadata resident;
2. read contiguous SD ranges where possible;
3. avoid tiny repetitive filesystem operations;
4. prefetch predictable navigation;
5. virtualize large UI collections;
6. stream one-pass data instead of caching it unnecessarily;
7. avoid writing transient state repeatedly;
8. shrink caches before starving executable/UI memory;
9. never hold mappings longer than required;
10. exploit e-paper refresh/idle intervals for background I/O.

---

## 36. Example: App Store catalog

A large App Store catalog should behave as:

```text
SD-backed catalog
       |
       | logical query/window
       v
storage VM / database cache
       |
       | nearby records
       v
virtualized List
       |
       | visible rows only
       v
UI controls
```

The number of catalog entries SHALL have little relationship to resident UI memory consumption.

---

## 37. Example: e-book reader

```text
Book on SD
    |
    +---- index metadata (small/hot)
    |
    +---- source content
    |
    +---- images
             |
             v
       storage/streaming layer
             |
             v
       current page working set
             |
             v
           renderer
             |
             v
          e-paper
```

The framework can retain the current page and neighboring content while discarding old decoded pages.

---

## 38. Example: scene eviction

Before:

```text
PSRAM

Catalog ELF
Catalog UI
Catalog mapped model pages
Detail ELF
Detail UI
Detail mapped data
```

After memory pressure while Detail is active:

```text
PSRAM

Detail ELF
Detail UI
Detail active mapped data
small VM cache

SD / serialized state

Catalog restoration state
Catalog model
Catalog resources
```

Back navigation reloads the Catalog controller and reconstructs only its active working set.

---

## 39. API direction

The first ABI SHOULD remain intentionally small.

Candidate API surface:

```c
t5_vm_handle_t t5_vm_open(...);
t5_vm_handle_t t5_vm_create(...);
int             t5_vm_close(...);
int             t5_vm_map(...);
int             t5_vm_unmap(...);
int             t5_vm_prefetch(...);
int             t5_vm_flush(...);
int             t5_vm_sync(...);
int             t5_vm_stat(...);
```

Additional database, document, image, and collection APIs SHOULD be built above this layer rather than bloating the primitive VM ABI.

---

## 40. Relationship to conventional virtual memory

Conventional demand-paged VM generally presents:

```text
ordinary pointer
      |
CPU memory access
      |
page fault if absent
      |
OS loads backing page
      |
access resumes transparently
```

T5 storage VM presents:

```text
storage handle + offset
      |
explicit map
      |
framework ensures resident window
      |
temporary ordinary pointer
      |
unmap
```

The result is similar in purpose—keeping only a working set resident—but deliberately different in mechanism.

Documentation SHALL call this **storage-backed virtual memory**, **storage VM**, or **virtual object memory**, and SHALL avoid claiming transparent hardware demand paging.

---

## 41. Relationship to future process isolation

The storage VM complements but does not create process isolation.

A future T5 application execution context may combine:

```text
T5 process/application context

identity
resident memory arena
ELF mapping
stack
handle table
VM-object handles
capabilities
timers
UI resources
```

Storage VM handles naturally fit a descriptor/handle-based process model because persistent state is represented by IDs and offsets rather than cross-domain raw pointers.

If hardware-assisted application compartments are later implemented, the VM service can copy/map requested windows only into the permitted application arena.

---

## 42. Boot and recovery

The VM subsystem SHALL distinguish cache files from durable application objects during startup.

Reconstructable cache corruption SHOULD normally result in cache deletion/rebuild rather than application failure.

Durable objects SHOULD use format/version checks and, where required, journal/transaction mechanisms at a higher layer.

Incomplete temporary/writeback artifacts from interrupted operations SHALL be recoverable or safely discardable.

The framework SHOULD reserve enough free SD capacity for required metadata/writeback operations and report low-storage conditions before durable writes become impossible.

---

## 43. Testing requirements

At minimum, tests SHOULD prove that:

1. virtual objects larger than PSRAM can be accessed through bounded mappings;
2. cache misses correctly populate PSRAM from SD;
3. cache hits return correct content without unnecessary SD reads;
4. unpinned clean pages can be evicted;
5. pinned pages remain valid for their promised lifetime;
6. dirty durable pages are not silently discarded;
7. invalid handles are rejected;
8. out-of-range and overflowing mappings are rejected;
9. one application cannot use another application's VM handle;
10. cache size remains within configured budget;
11. memory pressure shrinks the VM cache;
12. inactive ELF unloading and cache eviction coexist correctly;
13. SD removal during a cache miss returns a controlled error;
14. SD removal with dirty durable data reports unsynced state;
15. application teardown revokes mappings and releases pins;
16. stale mapped pointers are detectable in development instrumentation where practical;
17. prefetch never prevents demand I/O from making progress;
18. sequential workloads achieve effective read-ahead;
19. random workloads do not force excessive read-ahead;
20. reconstructable cache can be discarded and rebuilt;
21. scene serialization contains handles/IDs rather than mapped pointers;
22. virtualized lists maintain bounded resident memory as logical item count grows;
23. large documents/images do not require full decoded residency;
24. writeback behavior survives reset according to the declared durability contract.

---

## 44. Normative architectural rules

1. **Internal SRAM is reserved for hot and hardware-constrained memory.**
2. **PSRAM is the primary large resident working-set and ELF execution tier.**
3. **SD is backing storage, not directly dereferenceable RAM.**
4. **Resident allocations return pointers; storage-backed objects return handles.**
5. **A storage-backed pointer exists only while an explicit view is mapped.**
6. **ELF stacks, `.data`, `.bss`, and ordinary anonymous heap remain resident in the initial architecture.**
7. **Large datasets should be represented as virtual objects, streams, or higher-level storage services.**
8. **Storage-VM cache consumption is bounded and subordinate to critical executable/UI memory needs.**
9. **Pinned views cannot be evicted during their promised lifetime.**
10. **Persistent state stores handles/IDs/offsets, never mapped pointers.**
11. **Every VM object and mapping has an owner and participates in resource accounting.**
12. **Durable and reconstructable data have different writeback/eviction semantics.**
13. **The framework must handle SD removal and I/O failure explicitly.**
14. **Virtualized framework controls must not instantiate data/UI proportional to arbitrarily large logical collections.**
15. **Memory pressure is centrally managed before uncontrolled allocation failure.**
16. **Storage VM does not imply process isolation or transparent MMU paging.**
17. **The logical data capacity of an application may greatly exceed its resident RAM working set.**
18. **RAM contains execution and the active working set; SD contains cold, durable, and reconstructable state.**

---

## 45. Implementation sequence

Recommended implementation order:

1. add centralized SRAM/PSRAM accounting and memory-pressure metrics;
2. define application/system memory ownership and budgets;
3. introduce opaque storage-VM handles;
4. implement read-only `open/map/unmap/close` over SD-backed files;
5. add a bounded PSRAM page/window cache;
6. add LRU/CLOCK-style clean-page eviction;
7. instrument hit rate, miss latency, pinned bytes, and SD throughput;
8. add prefetch/access-pattern hints;
9. integrate cache shrinking with framework memory pressure;
10. integrate scene suspension and application teardown with mapping revocation;
11. virtualize framework lists/collections against windowed model access;
12. integrate document/image workloads using streaming/windowed access;
13. add writable objects, dirty pages, flush/sync, and durability classes;
14. add per-application quotas and handle ownership enforcement;
15. add asynchronous prefetch/storage worker support where measurements justify it;
16. tune cache granularity and budgets using actual T5S3 hardware workloads.

The initial milestone should intentionally be **read-only storage-backed mapped windows with bounded PSRAM caching**. That proves the memory model without prematurely taking on crash-consistent writable virtual memory.
