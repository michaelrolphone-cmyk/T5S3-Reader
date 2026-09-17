# T5 Platform Abstraction and Execution Runtime Architecture

## Status

Architecture specification for making T5 the application programming model rather than exposing ESP32-S3, FreeRTOS, ESP-IDF, PSRAM, SD, e-paper, or hardware implementation details to ordinary application and service code.

This specification complements `SCENE_RUNTIME_ARCHITECTURE.md`, `MEMORY_ARCHITECTURE.md`, the runtime driver architecture, security architecture, and future service runtime architecture.

> **Core invariant:** Applications and services express intent, state, events, capabilities, and durable data. T5 owns execution, concurrency, memory placement, storage, hardware, power, scheduling, cleanup, and presentation policy.

---

## 1. Goal

The primary platform goal is to make feature development independent of hardware and RTOS mechanics.

Ordinary T5 feature code SHOULD NOT need to know:

- which ESP32 core executes work;
- that FreeRTOS tasks, queues, mutexes, or semaphores exist;
- whether memory came from internal SRAM or PSRAM;
- whether large data is currently resident or SD-backed;
- which UART, SPI, I2C, GPIO, radio, or peripheral implements a capability;
- how Wi-Fi is powered or reconnected;
- how e-paper partial/full refresh policy works;
- how long-lived deadlines survive sleep;
- how resources are reclaimed after an ELF unloads;
- how drivers or services are discovered and loaded.

The desired programming model is:

```text
Feature code
    |
    v
State + Actions + Events + Capabilities + Data
    |
    v
T5 Platform APIs
    |
    +-- Execution Runtime
    +-- Data Runtime
    +-- Capability Runtime
    +-- Service Runtime
    +-- Presentation Runtime
    +-- Power Runtime
    +-- Scheduler
    |
    v
Drivers / ESP-IDF / FreeRTOS / Hardware
```

---

## 2. Architectural boundary

T5 SHALL define a stable platform boundary above ESP-IDF and FreeRTOS.

```text
+--------------------------------------------------+
| Applications / Scene Controller ELFs             |
| Service ELFs                                     |
+--------------------------------------------------+
| T5 Feature API                                   |
| state | events | jobs | data | capabilities      |
+--------------------------------------------------+
| T5 Platform Runtime                              |
| execution | resources | storage | power | UI     |
| scheduling | services | capability resolution    |
+--------------------------------------------------+
| Driver Capability ELFs                           |
+--------------------------------------------------+
| ESP-IDF / FreeRTOS                               |
+--------------------------------------------------+
| ESP32-S3 / board hardware                        |
+--------------------------------------------------+
```

Direct ESP-IDF/FreeRTOS/hardware access from ordinary app/service ELFs SHOULD be treated as an escape hatch, not the normal ABI.

---

## 3. Module classes

T5 SHALL distinguish at least three native ELF roles:

### Application/scene ELF
User-facing feature behavior. Governed by application/scene lifecycle and disposable residency.

### Service ELF
Background/system behavior managed independently of foreground UI. Services are scheduled and invoked by the Service Runtime rather than owning unrestricted permanent tasks.

### Driver ELF
Privileged hardware capability provider. Drivers translate T5 capability contracts into board/peripheral operations.

These roles SHALL have separate manifests, ABIs, lifecycle rules, permissions, and resource policies.

---

## 4. Execution Runtime

The Execution Runtime is the primary abstraction over FreeRTOS concurrency.

Applications and most services SHALL NOT need to create or manage FreeRTOS tasks, mutexes, queues, semaphores, priorities, task stacks, or core affinity.

Feature code SHOULD use event-driven lifecycle entry points and asynchronous jobs.

Conceptually:

```c
void on_appear(void);
void on_action(const t5_action_t *action);
void on_event(const t5_event_t *event);
void on_disappear(void);
```

Long-running/blocking work SHALL be submitted to the T5 executor rather than performed on the presentation/event dispatch path.

---

## 5. Asynchronous jobs

T5 SHALL expose asynchronous operations through opaque job handles.

Conceptually:

```c
t5_job_t job = t5_http_get(request);
t5_job_cancel(job);
```

Completion is delivered through framework events/messages:

```text
JOB_STARTED
JOB_PROGRESS
JOB_COMPLETE
JOB_FAILED
JOB_CANCELLED
```

The runtime owns the implementation mechanism, including worker tasks, queues, synchronization, stack allocation, priority, core placement, DMA waits, and blocking I/O.

Applications SHALL NOT assume a job maps one-to-one to a FreeRTOS task.

---

## 6. Executor classes

Internally T5 MAY maintain execution classes such as:

```text
presentation/event executor
I/O executor
CPU/background executor
system executor
driver execution context
```

These are implementation details. Public APIs SHALL describe workload intent/priority rather than FreeRTOS primitives.

The executor SHALL support cancellation, ownership, timeout/deadline metadata, and cleanup when the owning context dies.

---

## 7. Unified event/message model

T5 SHALL define a common event vocabulary used across applications, services, jobs, capabilities, and system state.

Initial event families SHOULD include:

```text
Lifecycle
  APPEAR
  DISAPPEAR
  SUSPEND
  RESUME

Input
  ACTION
  TOUCH
  KEY

Async work
  JOB_STARTED
  JOB_PROGRESS
  JOB_COMPLETE
  JOB_FAILED
  JOB_CANCELLED

System
  NETWORK_CHANGED
  TIME_CHANGED
  STORAGE_CHANGED
  POWER_CHANGED
  SYSTEM_SLEEPING
  SYSTEM_RESUMED

Capability
  CAPABILITY_AVAILABLE
  CAPABILITY_CHANGED
  CAPABILITY_LOST
```

Opaque handles and messages SHALL be preferred over storing arbitrary cross-ELF callback pointers. This supports ELF unloading, revocation, and future isolation.

---

## 8. Resource Context

Every executing app, scene, service, and other managed module SHALL execute under a framework-owned resource context.

Conceptually:

```text
T5Context
  identity
  memory allocations
  VM mappings
  files/storage objects
  jobs
  timers/deadlines
  event subscriptions
  capabilities
  power leases
  network operations
  UI objects
  service/driver handles
```

All acquired resources SHOULD be attributable to an owner context.

Destroying a context SHALL reclaim all framework-owned resources associated with it, even if feature code fails to explicitly release each one.

This is the platform-level lifetime/cleanup boundary.

---

## 9. Opaque handles

Feature ABIs SHOULD use opaque handles instead of exposing framework object pointers.

Examples:

```c
t5_file_t
t5_job_t
t5_timer_t
t5_capability_t
t5_subscription_t
t5_power_lease_t
t5_store_t
```

Handles SHALL be validated for type, ownership, generation/staleness, and permissions where appropriate.

Framework internals SHALL NOT be traversable through ordinary public handles.

---

## 10. Memory by intent

Feature code SHOULD request memory according to semantics rather than physical memory type.

It SHOULD NOT normally request ESP-IDF capability flags such as internal/PSRAM/DMA directly.

Conceptual classes:

```text
TRANSIENT     short-lived ordinary working memory
RESIDENT      must remain directly addressable
LARGE         large active working set
DMA           requires hardware-compatible addressability
CACHE         reconstructable and aggressively reclaimable
PERSISTENT    durable storage-backed data
VIRTUAL       potentially much larger than RAM
```

T5 maps these intentions onto the tiered memory architecture:

```text
small/hot/critical -> internal SRAM
large resident     -> PSRAM
ELF executable     -> executable-capable mapping
large/cold         -> SD-backed virtual object + PSRAM window
persistent         -> managed SD storage
DMA                -> compatible resident memory
```

The exact physical placement is not part of the feature API contract unless hardware semantics require it.

---

## 11. Data Runtime

T5 SHALL provide managed logical storage so ordinary applications/services do not need to construct filesystem layouts for routine state.

Each installed bundle/service SHALL receive namespaced storage classes such as:

```text
preferences
durable application data
documents
cache
temporary
virtual objects
```

Conceptual APIs:

```c
t5_preferences_get(...);
t5_preferences_set(...);
t5_store_open("history", ...);
t5_cache_open("thumbnails", ...);
t5_vm_open(...);
```

The runtime owns physical paths, quotas, cleanup, migration metadata, and storage policy.

Bundle replacement SHALL be separable from mutable data replacement.

---

## 12. Storage-backed large data

The Data Runtime SHALL integrate the storage-backed virtual memory architecture.

Large collections, documents, image resources, indexes, download data, and other cold datasets SHOULD use bounded resident windows rather than requiring complete PSRAM residency.

Applications SHALL operate using stable logical handles/identifiers and temporary mapped views.

Physical SD paging/cache behavior remains a runtime implementation detail.

---

## 13. Transactions

The Data Runtime SHOULD provide transactional operations for fragile durable state.

Conceptually:

```c
t5_transaction_begin(...);
t5_transaction_commit(...);
t5_transaction_abort(...);
```

Higher-level storage APIs SHOULD provide atomic replacement/crash recovery so feature code does not independently implement temporary files, backup files, rename protocols, journals, or recovery conventions.

---

## 14. Virtualized collections

The framework SHOULD provide collection/list primitives that decouple logical collection size from rendered/resident size.

A collection data source SHOULD conceptually expose operations such as:

```text
count
item(index)
action(index)
```

The framework owns:

```text
scrolling
visible-range calculation
row/view reuse
selection/focus
pagination
prefetch
storage-VM windowing
restoration
render invalidation
```

A 10-item list and a 100,000-item list SHOULD use the same feature-level model.

---

## 15. Capability Runtime

Capabilities SHALL be the normal abstraction for hardware and system facilities.

Capabilities may be provided by firmware, drivers, or services.

Examples:

```text
Hardware
  position.gnss
  battery.status
  usb.serial

System
  network.internet
  time.clock
  time.synchronized
  alarm.scheduler
  notifications
  system.update

Higher-level
  search.index
  media.playback
```

Feature code requests a capability by contract. It SHALL NOT need to know whether the provider is built-in firmware, a driver ELF, or a service ELF.

The Capability Runtime resolves and manages providers.

---

## 16. Capability leases

Capabilities that imply scarce hardware, power, or provider residency SHOULD be acquired through leases.

Conceptually:

```c
t5_capability_t gnss = t5_capability_acquire("position.gnss", ...);
t5_capability_release(gnss);
```

While a lease exists, the runtime ensures the provider and required dependencies are available subject to policy.

When leases disappear, the runtime MAY suspend/unload providers and power down hardware.

---

## 17. Service Runtime

Background/system work SHALL be implemented through managed service ELFs rather than unrestricted permanent application tasks.

Service execution modes SHOULD include:

```text
resident
event-driven
scheduled
periodic
on-demand
boot
idle/opportunistic
```

The Service Manager owns lifecycle, scheduling, dependency resolution, resource budgets, restart policy, persistence, and unloading.

A service SHOULD perform bounded work and become unloadable whenever continuous residency is unnecessary.

Examples include time synchronization, alarm handling, update checks, indexing, background downloads, and notification processing.

---

## 18. Persistent scheduling

T5 SHALL provide system-owned scheduling independent of service ELF residency.

Conceptual APIs:

```c
t5_schedule_at(...);
t5_schedule_after(...);
t5_schedule_periodic(...);
t5_schedule_cancel(...);
```

The scheduler determines whether a deadline uses an awake software timer, persistent deadline state, RTC/deep-sleep wakeup, or restoration after reboot.

An alarm service SHALL NOT need to remain loaded merely to wait for an alarm deadline.

---

## 19. Power Runtime

Power policy SHALL be centralized.

Apps/services SHOULD declare temporary requirements rather than directly controlling global sleep or peripheral power.

Conceptually:

```c
t5_power_lease_t lease = t5_power_acquire(T5_POWER_REQUIRE_NETWORK);
...
t5_power_release(lease);
```

The runtime owns:

- sleep eligibility;
- peripheral/provider power transitions;
- network wake requirements;
- display wake policy;
- deep-sleep scheduling;
- conflicting requirement arbitration;
- cleanup of leaked leases when an owner context terminates.

No app/service SHOULD be able to accidentally prevent sleep forever merely because it forgot a raw firmware power lock.

---

## 20. Networking Runtime

Ordinary feature code SHOULD use high-level asynchronous network operations rather than sockets/Wi-Fi lifecycle directly.

Initial abstractions SHOULD include:

```text
HTTP request
download/upload
network availability
optional WebSocket/streaming abstraction
```

The runtime owns Wi-Fi acquisition, connection state, DNS, TLS, buffering, timeout, retry policy, storage-backed downloads where appropriate, cancellation, and power integration.

Low-level sockets MAY remain an advanced capability rather than the default API.

---

## 21. Presentation Runtime

Applications manipulate framework UI state/objects; the framework owns e-paper rendering mechanics.

Feature code SHOULD NOT normally choose partial versus full refresh, directly drive the panel, or maintain ghosting policy.

The Presentation Runtime owns:

```text
view hierarchy
layout
navigation chrome
Back behavior
scrolling
focus/input routing
dirty regions
partial/full refresh policy
ghosting budget
framebuffer/display transactions
```

State changes invalidate logical presentation. The runtime determines the cheapest correct physical update.

---

## 22. State-driven UI integration

This specification retains the scene-runtime invariant that authoritative state lives outside disposable controllers/rendered views.

```text
Application/model state
       +
NavigationPath/scene state
       |
       v
active controller ELF
       |
       v
framework UI objects
       |
       v
Presentation Runtime
```

State-driven architecture SHALL NOT require continuous rebuilding or redrawing. Active concrete UI may remain resident and be incrementally updated.

---

## 23. Cancellation

Every asynchronous framework operation that can outlive its immediate caller SHOULD have explicit cancellation semantics.

Cancellation SHALL also occur automatically when required by owner-context destruction.

A cancelled job SHALL eventually reach a terminal state and release framework-owned resources.

Feature code SHALL NOT need to locate and terminate underlying FreeRTOS tasks.

---

## 24. Timeouts and deadlines

Blocking or potentially unbounded platform operations SHOULD support runtime-controlled timeout/deadline policy.

Services SHOULD have execution budgets so malfunctioning background code cannot consume CPU, memory, network, or power indefinitely.

Timeout outcomes SHALL be delivered as structured errors/events rather than requiring application watchdog logic.

---

## 25. Fault containment

T5 SHOULD convert recoverable module/runtime failures into managed lifecycle outcomes whenever possible.

Examples:

```text
invalid/stale handle
resource quota exceeded
job timeout
service timeout
ELF load/entry failure
ABI mismatch
capability unavailable
provider failure
storage failure
```

The runtime SHOULD surface outcomes such as:

```text
APP_FAILED
SERVICE_FAILED
DRIVER_FAILED
JOB_FAILED
CAPABILITY_LOST
```

This does not imply complete hardware memory isolation. Native-code memory corruption may remain capable of compromising the system until stronger protection is implemented.

---

## 26. Resource budgets and quotas

Managed contexts SHOULD support accounting for:

```text
resident SRAM
resident PSRAM
storage-VM mappings/cache
open handles
jobs
CPU/runtime budget
timers/deadlines
network activity
power leases
wakeups
persistent storage
```

System policy MAY assign different limits to apps, services, and privileged drivers.

A single module SHALL NOT be allowed to accidentally consume all framework resources through managed APIs.

---

## 27. Hardware independence

Feature-level capability contracts SHOULD avoid board-specific concepts whenever possible.

For example, applications should consume:

```text
position.gnss
```

rather than:

```text
UART1 RX GPIO 44 at 9600 baud
```

Board/device-specific details belong in driver/provider manifests and implementation.

This SHALL allow future T5 hardware to provide the same capability through different silicon without changing application code.

---

## 28. Escape hatches

T5 MAY expose advanced low-level APIs for specialized software, but they SHALL be explicit privileged capabilities rather than the default development path.

Examples may include raw sockets, raw serial, raw filesystem paths, or direct hardware buses.

Use of escape hatches MAY reduce portability, resource guarantees, power optimization, and future isolation.

---

## 29. Developer programming model

A normal application should primarily consist of:

```text
models/state
scene controllers
user actions
event handling
capability requests
async job requests
logical data access
```

A normal service should primarily consist of:

```text
event/schedule triggers
bounded work
capability requests
logical persistent state
result/event publication
```

Neither should normally contain RTOS scheduling, hardware initialization, memory-tier selection, sleep coordination, or display-driver logic.

---

## 30. Example feature flow

A weather-style feature SHOULD conceptually be expressible as:

```text
scene appears
    |
    v
acquire network/service capability
    |
    v
request asynchronous refresh
    |
    v
return to framework

... runtime handles threads/network/power ...

JOB_COMPLETE / MODEL_CHANGED
    |
    v
update model
    |
    v
framework invalidates affected UI
    |
    v
presentation runtime selects e-paper update
```

The feature does not know which task executed the HTTP request, where buffers lived, how Wi-Fi was powered, or which physical refresh mode was selected.

---

## 31. Platform-owned responsibilities

The following SHOULD remain core runtime responsibilities rather than arbitrary pluggable feature code:

```text
boot
ELF loader
signature verification
execution/runtime scheduler
resource contexts/handle tables
event/message bus
persistent deadline scheduler
memory manager
storage/data runtime
capability resolver
service manager
driver manager
power/sleep coordinator
presentation/display ownership
crash/watchdog infrastructure
basic filesystem substrate
```

Higher-level behavior may be implemented as replaceable services/drivers on top of these primitives.

---

## 32. Relationship to security/isolation

The abstraction boundary SHALL be designed so it can later become a stronger security boundary.

Therefore public APIs SHOULD:

- use handles rather than framework pointers;
- validate buffer ranges and lengths;
- associate resources with identities/contexts;
- avoid arbitrary cross-ELF callbacks;
- centralize privileged hardware access;
- centralize service/driver loading;
- enforce declared capabilities;
- permit provider revocation/unloading.

These practices are valuable even before hardware-enforced process isolation exists.

---

## 33. Observability

The runtime SHOULD expose diagnostics sufficient to debug abstractions without requiring applications to instrument FreeRTOS directly.

Diagnostics SHOULD include:

```text
active contexts
resident modules
job queue/running jobs
resource ownership
memory by context/tier
VM cache pressure
open capabilities/providers
service states
scheduled deadlines
power leases
network operations
recent failures
```

A future system diagnostics application can consume this data through a privileged framework capability.

---

## 34. Implementation sequence

### Phase 1 - Resource context and handles

Create `T5Context`, ownership tracking, typed/generation-safe handles, and deterministic cleanup. Migrate framework APIs to associate resources with a context.

### Phase 2 - Execution Runtime

Create the framework executor, async job abstraction, cancellation, terminal job states, and unified event delivery. Migrate app code away from direct task creation where practical.

### Phase 3 - Event bus

Define versioned system/job/capability event structures and subscription ownership. Eliminate long-lived cross-ELF callbacks where possible.

### Phase 4 - Capability and power leases

Unify capability acquisition with resource ownership. Make hardware/provider residency and power requirements lease-driven.

### Phase 5 - Data Runtime

Add namespaced preferences, durable data, cache/temp storage, transactions, and integration with storage-backed virtual objects.

### Phase 6 - Persistent scheduler and Service Runtime

Implement durable deadlines, event/scheduled service activation, resource budgets, persistence, restart policy, and unloading. Use time synchronization as the first reference service, updater as the second, and alarm handling as the scheduled/deep-sleep validation case.

### Phase 7 - Networking Runtime

Move common network operations behind async T5 APIs with automatic connectivity, timeout, cancellation, buffering, storage, and power integration.

### Phase 8 - Presentation abstraction

Complete framework ownership of navigation, lists/scrolling, input, dirty regions, e-paper refresh policy, and virtualization.

### Phase 9 - Isolation hardening

Use the established context/handle/capability boundaries to add available ESP32-S3 memory protection, stricter module privileges, signed service policy, quotas, and fault containment.

---

## 35. Acceptance criteria

The abstraction architecture is successful when representative applications and services can be implemented without direct use of:

```text
xTaskCreate
vTaskDelete
FreeRTOS queues/semaphores/mutexes
ESP heap capability selection
raw PSRAM allocation policy
raw SD filesystem paths for routine state
raw Wi-Fi lifecycle
raw display refresh calls
raw sleep/power locks
raw UART/SPI/I2C/GPIO for ordinary capabilities
```

A representative app SHALL be able to perform UI, navigation, persistence, networking, large-data access, scheduling, and hardware capability consumption exclusively through T5 APIs.

A representative background service SHALL be able to wake from an event/deadline, perform bounded asynchronous work, persist state, publish results, and become unloadable without owning permanent RTOS machinery.

---

## 36. Architectural test applications

The implementation SHOULD maintain small reference modules that prove abstraction completeness:

1. **Weather/reference network app** - UI + HTTP + persistence + async jobs + power.
2. **Large catalog app** - virtualized collection + storage-backed data + scrolling.
3. **Time synchronization service** - event/scheduled service + network + time capability.
4. **Updater service** - background network + durable state + large download + transaction.
5. **Alarm service** - persistent deadline + deep sleep/wake + event delivery.
6. **GNSS app** - hardware capability consumption without UART knowledge.

If any reference feature requires ordinary application code to reach through the T5 boundary into ESP-IDF/FreeRTOS, the missing abstraction SHOULD be treated as a platform design gap.

---

## 37. Final architecture

```text
                 T5 FEATURE SOFTWARE

       Apps / Scenes              Services
            |                        |
            +----------+-------------+
                       |
          State / Actions / Events / Data
                       |
                       v
+--------------------------------------------------+
|                  T5 FEATURE API                  |
+--------------------------------------------------+
| Execution | Resource Contexts | Event Bus        |
| Data/VM   | Scheduler         | Networking       |
| Power     | Presentation      | Capabilities     |
| Service Manager              | Security          |
+----------------------+---------------------------+
                       |
               Capability Resolver
                       |
          +------------+-------------+
          |                          |
    Built-in providers         Driver ELFs
          |                          |
          +------------+-------------+
                       |
                ESP-IDF / FreeRTOS
                       |
                    Hardware
```

The intended development experience is therefore:

> **Feature software describes what should happen. T5 decides how the machine makes it happen.**

That principle SHALL guide future framework APIs and migration of existing firmware functionality.