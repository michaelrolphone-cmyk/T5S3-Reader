# T5S3 Pluggable System Service Runtime Architecture

## Status

Architecture specification for dynamically loadable ELF system services and background workers in the T5S3 platform.

This specification defines **service ELFs** as a third native module class alongside application/scene ELFs and hardware-driver ELFs. Services perform system work independently of the foreground application while remaining under framework ownership for scheduling, lifecycle, power, memory, security, and resource management.

> **Core invariant:** A service ELF supplies behavior. The T5 core owns when it runs, how long it runs, what resources it owns, what wakes it, and when it may be unloaded.

---

## 1. Goals

The service runtime SHALL:

1. allow system functionality to be installed and updated independently of firmware;
2. support background work without requiring every service ELF to remain resident;
3. support event-driven, scheduled, periodic, on-demand, boot-time, idle, and genuinely resident services;
4. permit services to consume and provide framework capabilities;
5. resolve dependencies by capability rather than hard-coded ELF paths;
6. provide persistent scheduling that survives application transitions, sleep, and restart where required;
7. permit exact alarm/deadline wakeups without keeping the alarm service resident;
8. centralize event delivery;
9. integrate with T5 power/sleep policy;
10. associate all runtime resources with service ownership;
11. impose CPU, memory, wakeup, network, and power budgets;
12. support service state persistence while allowing ELF eviction;
13. detect repeated failures and prevent service boot loops;
14. authenticate service ELFs under production security policy;
15. separate service authenticity from service authorization/privilege;
16. remain compatible with future application/process isolation.

---

## 2. Native module classes

T5 SHALL distinguish at least three ELF module classes:

```text
APP / SCENE ELF
    User-facing application behavior.
    Lifetime follows scene/application execution.

DRIVER ELF
    Privileged hardware capability provider.
    Lifetime follows hardware/capability demand.

SERVICE ELF
    System/background behavior.
    Lifetime follows events, schedules, capability demand,
    and Service Manager policy rather than foreground UI.
```

A module SHALL declare its type in authenticated package metadata. The loader SHALL reject module-type confusion.

A service ELF SHALL NOT be treated as an application merely because both use the ELF loader.

---

## 3. Platform architecture

```text
Applications / Scene Runtime
           |
           | capabilities / messages / events
           v
+------------------------------------------------+
|             T5 Service Manager                 |
|                                                |
| registry        dependency resolution          |
| lifecycle       scheduling integration         |
| event delivery  health/restart policy          |
| state restore   resource accounting            |
+----------------------+-------------------------+
                       |
        +--------------+---------------+
        |              |               |
        v              v               v
   time-sync.elf   alarms.elf      updater.elf
        |              |               |
        +--------------+---------------+
                       |
                       v
              Framework capabilities
                       |
                       v
                 Signed Drivers
                       |
                       v
                    Hardware
```

The Service Manager SHALL be firmware-resident core infrastructure. Individual services SHOULD be unloadable unless their execution model genuinely requires residency.

---

## 4. What remains in T5 core

The following machinery SHOULD remain firmware-resident rather than becoming ordinary service ELFs:

```text
boot/runtime initialization
ELF loader
signature/package verifier
Service Manager
persistent deadline scheduler
core event bus
resource manager
power/sleep coordinator
capability resolver
memory manager
basic filesystem/mount machinery
watchdog/crash infrastructure
minimum recovery/update bootstrap required to recover the platform
```

These components are required to safely discover, authenticate, schedule, execute, stop, and recover pluggable modules.

Higher-level functionality is a candidate for a service ELF.

---

## 5. Service examples

Candidate services include:

```text
time synchronization
alarm scheduling/handling
background firmware/application update checks
background downloads
notification aggregation
content/library indexing
cache maintenance
network synchronization
cloud/account synchronization
device discovery
periodic telemetry where enabled
scheduled maintenance
```

Hardware-specific implementation belongs in drivers rather than services.

For example, time synchronization consumes a network capability and updates the system time through an authorized framework API. It does not own the Wi-Fi hardware driver.

---

## 6. Service package layout

Installed services SHOULD use a dedicated namespace:

```text
/Services/
    time-sync/
        manifest.json
        service.elf
        signature.bin

    alarms/
        manifest.json
        service.elf
        signature.bin

    updater/
        manifest.json
        service.elf
        signature.bin
```

Development policy MAY permit unsigned packages. Signed/production policy SHALL follow `SECURITY_ARCHITECTURE.md`.

The package container used for distribution is not a security boundary.

---

## 7. Service manifest

A service manifest SHALL describe identity, execution policy, capabilities, subscriptions, compatibility, and resource requests.

Conceptual example:

```json
{
  "schema": 1,
  "id": "time-sync",
  "type": "service",
  "version": "1.0.0",
  "minimum_firmware": "1.3.0",
  "module": "service.elf",

  "execution": {
    "mode": "event",
    "autostart": true
  },

  "consumes": [
    "network.internet",
    "time.clock"
  ],

  "provides": [
    "time.synchronized"
  ],

  "events": [
    "network.connected",
    "system.resume"
  ],

  "resources": {
    "max_resident_bytes": 65536,
    "max_runtime_ms": 15000
  },

  "restart": {
    "policy": "limited",
    "max_failures": 3
  }
}
```

The exact schema SHALL be versioned before implementation.

Under signed policy, security-sensitive manifest fields SHALL be authenticated as specified by the security architecture.

---

## 8. Service execution modes

The Service Manager SHALL support explicit execution models.

### 8.1 Resident

Loaded and active for extended periods because continuous execution is intrinsically required.

Use sparingly.

### 8.2 Event-driven

Loaded or activated when a subscribed event occurs.

Example:

```text
network.connected
       |
       v
load time-sync service
       |
       v
synchronize clock
       |
       v
persist state / schedule next check
       |
       v
unload
```

### 8.3 Scheduled

Activated at a specific registered deadline.

### 8.4 Periodic

Activated according to a framework-owned recurrence schedule.

### 8.5 On-demand

Activated because another component requests a capability the service provides.

### 8.6 Boot

Runs during a defined startup phase, performs bounded work, and normally terminates.

### 8.7 Idle

Runs opportunistically when the platform has sufficient power, memory, and idle time. Idle work SHALL yield to foreground work and sleep policy.

A service MAY combine compatible triggers, but the Service Manager remains authoritative over actual activation.

---

## 9. No permanent task assumption

A service ELF SHALL NOT imply a permanently allocated FreeRTOS task.

The framework MAY create a task only while executing service work and destroy/reuse it afterward.

Services SHOULD normally follow:

```text
trigger
   |
   v
validate activation conditions
   |
   v
load ELF if absent
   |
   v
create/restore service context
   |
   v
execute bounded work
   |
   v
persist required state
   |
   v
release resources
   |
   v
unload ELF when policy permits
```

This is the default model for time sync, update checking, indexing, and similar work.

---

## 10. Persistent scheduler

Scheduling SHALL be a core T5 facility, not functionality implemented independently inside every service.

A service requests a deadline through the host ABI:

```text
service
   |
   | schedule(deadline, event)
   v
T5 persistent scheduler
   |
   v
sleep/wake coordinator
```

The scheduler SHALL retain enough information to deliver due work after sleep/restart when the schedule's durability policy requires it.

A service does not need to remain loaded merely because it has future work scheduled.

---

## 11. Alarm-clock model

Alarm clocks demonstrate why scheduler ownership belongs in core.

```text
alarms.elf
    |
    | register alarm 42 for 07:00
    v
T5 persistent deadline scheduler
    |
    v
service unloads
    |
    v
deep sleep
    |
    v
hardware/RTC wake source
    |
    v
T5 resumes/boots
    |
    v
scheduler detects alarm 42 due
    |
    v
load alarms.elf
    |
    v
deliver scheduled event
    |
    v
alarm service handles notification/UI/audio/etc.
```

The scheduler SHALL coordinate the earliest required wake deadline with the power manager.

Alarm correctness SHALL NOT depend on the service task remaining alive.

---

## 12. Schedule classes

The scheduler SHOULD distinguish scheduling intent.

Conceptual classes:

```text
EXACT
    User-visible deadline such as an alarm.

FLEXIBLE
    May run within a tolerance window to coalesce wakeups.

PERIODIC
    Repeating work with framework-controlled drift/catch-up policy.

IDLE
    No deadline; execute opportunistically.
```

Background update checks SHOULD generally be flexible. Alarm clocks are exact.

The scheduler SHOULD coalesce flexible work to reduce wakeups and power consumption.

---

## 13. Time semantics

Scheduling SHALL explicitly distinguish:

- monotonic elapsed time;
- UTC/system wall-clock time;
- local civil time/time zone;
- user-visible recurring local-time alarms.

A wall-clock correction from NTP SHALL NOT accidentally invalidate monotonic timeout semantics.

Alarm services SHALL define behavior for time-zone changes, daylight-saving transitions, clock corrections, and missed deadlines.

The core scheduler should provide primitive time/deadline semantics; user-facing alarm recurrence policy may remain in the alarm service.

---

## 14. Event bus

T5 SHALL provide a core event-distribution mechanism usable by framework components, drivers, services, and applications according to authorization policy.

Example events:

```text
system.boot
system.ready
system.resume
system.sleeping
system.time_changed

network.connected
network.disconnected

power.charging
power.discharging
power.low

storage.mounted
storage.removed

application.installed
application.removed

update.available
```

Event names and payload schemas SHALL be versioned.

Services SHOULD declare static subscriptions in their manifests so the Service Manager can activate an unloaded service when an event of interest occurs.

---

## 15. Event delivery to unloaded services

The event bus SHALL NOT require every subscriber ELF to remain resident.

Conceptually:

```text
network.connected
       |
       v
Event Bus
       |
       v
Service registry finds subscribed time-sync
       |
       v
Service Manager loads time-sync.elf
       |
       v
delivers event
```

The registry therefore stores subscription metadata independently of ELF residency.

---

## 16. Event durability

Events SHALL declare delivery semantics.

Possible classes:

```text
TRANSIENT
    Relevant only if a consumer is currently able to run.

COALESCED
    Multiple equivalent events may collapse into one pending event.

DURABLE
    Must remain pending until delivered/acknowledged according to policy.
```

For example, repeated network-state notifications may be coalesced. A user alarm due event may require durable handling.

The event bus SHALL have bounded queues. Services SHALL NOT be able to cause unbounded event accumulation.

---

## 17. Message-oriented boundaries

The service architecture SHOULD prefer handles, messages, events, and capability calls over arbitrary cross-module callback pointers.

Preferred:

```text
service -> event bus -> consumer
service -> capability handle -> framework
```

Avoid long-lived privileged structures containing raw function pointers into unloadable service ELFs.

Any callback mechanism that is retained SHALL be scoped so unloading a module deterministically invalidates and removes all callbacks.

This is important for ELF eviction and future isolation.

---

## 18. Capability consumption

Services SHALL request system functionality through capabilities/framework APIs rather than direct implementation knowledge.

Example:

```text
time-sync service
       |
       | require network.internet
       v
Capability resolver
       |
       v
network provider/service/driver
```

A service SHALL NOT directly `dlopen()` another service or driver by filesystem path.

This is prohibited:

```c
dlopen("/Services/network/service.elf", RTLD_NOW);
```

The service instead declares or requests the required capability.

---

## 19. Service-provided capabilities

A service MAY itself provide a capability.

Examples:

```text
time-sync service
    provides: time.synchronized

alarm service
    provides: alarm.scheduler

update service
    provides: system.update
```

Capability registration SHALL be performed by the Service Manager only after the service package is validated and authorized.

Capability availability MAY trigger activation of dependent components.

---

## 20. Dependency graph

Service capability dependencies form a directed graph.

Example:

```text
network.wifi
     |
     v
network.internet
     |
     v
time-sync
     |
     v
time.synchronized
     |
     +----------+
     |          |
     v          v
 alarms      updater/security-time consumers
```

The Service Manager/capability resolver SHALL detect unsatisfied dependencies and dependency cycles that cannot be resolved safely.

A service failure SHALL NOT automatically crash dependent services; dependent capabilities should become unavailable or degraded according to policy.

---

## 21. Service lifecycle

The framework SHALL define service lifecycle states independently of ELF residency.

Recommended states:

```text
DISCOVERED
VALIDATED
WAITING
STARTING
RUNNING
SUSPENDING
SUSPENDED
STOPPING
FAILED
DISABLED
QUARANTINED
```

A service can be `WAITING` while its ELF is completely unloaded.

The registry and scheduler retain logical service state independently of executable residency.

---

## 22. Service ABI

The initial service ABI SHOULD remain small and versioned.

Conceptual interface:

```c
typedef struct {
    uint32_t abi_version;

    int (*create)(
        const t5_service_host_v1 *host,
        void **context);

    int (*start)(void *context);

    int (*event)(
        void *context,
        const t5_event_v1 *event);

    int (*prepare_suspend)(
        void *context,
        t5_state_writer_v1 *writer);

    int (*restore)(
        void *context,
        const void *state,
        size_t state_size,
        uint32_t state_version);

    void (*stop)(void *context);
    void (*destroy)(void *context);

} t5_service_api_v1;
```

The exact exported entry-point contract SHALL follow the platform's ELF-loader conventions.

The host interface SHALL expose only approved framework functionality.

---

## 23. Framework-owned resources

The service host SHALL own or track resources acquired by a service, including:

```text
tasks/work items
timers
scheduled deadlines
event subscriptions
file handles
storage-VM handles/mappings
network handles
capability leases
power locks
memory allocations
message queues
UI/notification resources where applicable
```

When a service stops, fails, is disabled, or unloads, the framework SHALL be able to reclaim its resources deterministically.

A service SHALL NOT rely on static destructors alone for system cleanup.

---

## 24. Service execution context

The runtime SHOULD maintain a service execution record analogous to an application resource context.

Conceptually:

```c
typedef struct {
    t5_service_id_t identity;
    uint32_t state;
    uint32_t abi_version;

    size_t resident_bytes;
    size_t peak_resident_bytes;

    t5_handle_table_t handles;
    t5_capability_set_t capabilities;

    uint32_t failure_count;
    uint64_t last_start;
    uint64_t last_success;

} t5_service_context_t;
```

The implementation SHALL avoid exposing internal pointers from this structure to service ELFs.

---

## 25. Persistent service state

Service state SHALL be separable from executable residency.

```text
service ELF
    |
    +---- transient execution state -> SRAM/PSRAM
    |
    +---- persistent state ----------> service store / SD
```

A service may therefore execute as:

```text
load ELF
   |
restore state
   |
perform work
   |
persist state
   |
release resources
   |
unload ELF
```

Persistent state SHALL contain stable values, handles/IDs, and serializable data rather than raw pointers.

State formats SHALL be versioned.

---

## 26. Storage-backed memory integration

Services SHALL use the tiered memory architecture defined by `MEMORY_ARCHITECTURE.md`.

Long-lived or large service data SHOULD reside in durable or reconstructable storage-backed objects rather than forcing the service ELF to remain resident.

Example updater state:

```text
last successful check
latest known version
ETag/cache validators
download progress
retry metadata
staged package identity
```

Only the active working set belongs in SRAM/PSRAM.

---

## 27. Resource budgets

Every service SHOULD have a system-enforced resource budget.

Relevant budgets include:

```text
maximum continuous runtime
CPU/work quantum
resident SRAM/PSRAM
storage-VM cache/pinned bytes
network transfer
open handles
power-lock duration
wakeup frequency
restart frequency
persistent storage quota
```

Manifest values are requests/hints constrained by system policy. A service cannot grant itself additional privilege or resources by editing its manifest.

---

## 28. Power policy

Background services SHALL participate in centralized power management.

A service SHALL NOT prevent sleep merely because its ELF is loaded.

Only explicit framework-issued power leases/locks may block or delay sleep.

Power locks SHALL:

- be owned by a service context;
- have a reason/type;
- preferably have a timeout/deadline;
- be automatically released on service teardown/failure;
- be visible in diagnostics.

The Service Manager SHOULD defer non-urgent work when battery/power policy requires it.

---

## 29. Wakeup policy

Services SHALL NOT directly program arbitrary hardware wake sources when a core scheduler/power API can represent the requirement.

Instead:

```text
service requests deadline
       |
       v
persistent scheduler
       |
       v
power manager
       |
       v
select/program appropriate wake source
```

This permits multiple service deadlines to be coalesced and prevents services from fighting over sleep configuration.

---

## 30. Foreground priority

Foreground user interaction SHALL have priority over ordinary background work.

The Service Manager SHOULD be able to pause, defer, or throttle non-critical services when:

- input/UI latency would suffer;
- memory pressure is high;
- e-paper rendering requires resources;
- another high-priority system operation is active;
- battery state requires conservation.

Exact alarms and security-critical operations may have elevated policy-defined priority.

---

## 31. Failure handling

A service failure SHALL be contained at the Service Manager level as far as the hardware/runtime permits.

The manager SHALL track failure counts and restart policy.

Supported restart policies SHOULD include:

```text
NEVER
ON_FAILURE
LIMITED
ALWAYS
```

Repeated failures SHOULD use backoff:

```text
failure
   |
   v
short delay
   |
failure
   |
   v
longer delay
   |
failure
   |
   v
quarantine/disable according to policy
```

A repeatedly failing optional service SHALL NOT create an endless device boot loop.

---

## 32. Quarantine

The Service Manager SHOULD support automatic quarantine when a service repeatedly crashes, violates resource policy, fails authentication after replacement, or otherwise prevents stable operation.

Quarantine state SHALL be retained independently of the service ELF.

Core recovery functionality SHALL remain available when optional services are quarantined.

Production policy SHALL define whether and how users/developers may clear quarantine.

---

## 33. Watchdog interaction

Service work SHALL execute under framework/watchdog supervision.

Long operations SHOULD yield, use asynchronous I/O, or be divided into bounded work units.

A service SHALL NOT be allowed to disable system watchdog protection through ordinary service APIs.

Watchdog diagnostics SHOULD identify the active service/work item where possible.

---

## 34. Security model

Service ELFs are native background system code and SHALL participate in the trust chain defined by `SECURITY_ARCHITECTURE.md`.

Conceptually:

```text
T5 root trust
     |
     +---- firmware authority
     |
     +---- driver authority
     |
     +---- service authority
     |
     +---- application authority (optional/future)
```

Production policy SHOULD require authenticated service packages.

A valid signature establishes package authenticity. It does not automatically grant unrestricted privileges.

---

## 35. Service authorization

Service privileges SHALL be explicitly authorized.

Example:

```text
time-sync service

requested/allowed:
    network.internet
    time.read
    time.set

not implicitly allowed:
    raw GPIO
    raw UART
    firmware flash
    unrestricted filesystem
```

The authenticated manifest may declare requested capabilities, but the trusted system policy determines what the signer/service identity is allowed to receive.

Services SHOULD receive opaque handles and narrow APIs rather than raw firmware subsystem pointers.

---

## 36. Trusted service signing

Service signing authority SHOULD be separable from driver and firmware signing authority.

This limits the impact of a compromised service release key.

Delegated signing MAY be permitted for third-party system services, but certificates/policy SHOULD constrain allowed service identities and capabilities.

A third-party clock synchronization service should not gain driver-level hardware privilege merely because its package signature is valid.

---

## 37. Native-code isolation limitation

As with driver ELFs, signature verification does not create process isolation.

Until hardware-assisted compartments or equivalent isolation are implemented, a signed native service containing memory corruption may damage framework state.

Therefore service ELFs SHALL be treated as privileged native modules relative to ordinary application logic, even when their capability API is narrow.

The architecture SHOULD remain compatible with a future isolated service/application execution domain by avoiding unnecessary raw pointer sharing.

---

## 38. Service discovery

At boot or storage mount, the Service Manager SHALL discover service manifests without loading every service ELF.

Discovery should populate a compact registry containing at least:

```text
service identity
package path
version
ABI compatibility
execution mode
subscriptions
provided capabilities
required capabilities
authentication/validation status
enabled/quarantined state
```

ELF loading occurs only when execution is required.

---

## 39. Registry persistence

The registry MAY be cached for startup performance, but mutable SD metadata SHALL NOT be trusted merely because it appeared in a previous cache.

Security-sensitive decisions SHALL follow the package verification policy.

The registry SHALL tolerate service removal/replacement between boots or SD mounts.

---

## 40. Startup phases

The platform SHOULD define explicit startup phases such as:

```text
CORE_INIT
STORAGE_READY
SERVICE_DISCOVERY
DRIVER_DISCOVERY
CAPABILITY_RESOLUTION
BOOT_SERVICES
SYSTEM_READY
FOREGROUND_READY
```

Services SHALL declare which events/capabilities they require rather than relying on incidental initialization order.

Boot services MUST remain bounded so a nonessential service cannot indefinitely prevent the system from reaching a usable UI.

---

## 41. Shutdown and sleep

Before sleep/shutdown, the Service Manager SHOULD:

1. stop admitting ordinary background work;
2. announce `system.sleeping` according to event policy;
3. allow bounded service state persistence;
4. flush required durable state;
5. revoke/release nonessential resources;
6. unload eligible service ELFs;
7. collect the next exact/flexible deadlines;
8. provide wake requirements to the power manager.

A service that fails to suspend within its budget SHALL not be permitted to block sleep indefinitely unless system policy explicitly marks its work critical.

---

## 42. Updates and atomic replacement

Service updates SHALL be treated as package-level replacements.

The update process SHOULD:

```text
stage new package
       |
verify package
       |
stop old service at safe point
       |
persist/migrate state if needed
       |
atomically activate new package
       |
start/validate new service when required
       |
rollback/quarantine on failure according to policy
```

An executing ELF SHALL NOT be overwritten in place.

State schema migration SHALL be explicit and versioned.

---

## 43. Reference service: time synchronization

Time synchronization SHOULD be the first reference service because it exercises the core model with limited complexity.

Expected behavior:

```text
system/network event
      |
      v
Service Manager activates time-sync
      |
      v
acquire network.internet
      |
      v
perform synchronization
      |
      v
set clock through authorized time API
      |
      v
publish time.synchronized / system.time_changed
      |
      v
schedule next flexible synchronization
      |
      v
persist status
      |
      v
release network/power resources
      |
      v
unload
```

It SHALL NOT require a permanently running task.

---

## 44. Reference service: background updater

The updater should validate:

- flexible periodic scheduling;
- network capability acquisition;
- storage-backed persistent state;
- resumable downloads;
- package verification;
- foreground notification/event delivery;
- resource and power budgets;
- retry/backoff;
- unloading between checks.

The updater SHALL not own Wi-Fi hardware directly.

A minimal recovery/update mechanism required to recover broken service packages SHOULD remain in trusted core firmware even if normal update policy is implemented by a service.

---

## 45. Reference service: alarm clock

The alarm service should validate:

- exact persistent deadlines;
- local civil-time semantics;
- deep-sleep wake coordination;
- durable due-event delivery;
- service activation after wake;
- user-facing alarm state independent of ELF residency.

The alarm service SHALL NOT remain resident merely to wait for time to pass.

---

## 46. Diagnostics

Development/system diagnostics SHOULD expose:

```text
installed services
validation/signature status
lifecycle state
ELF residency
last activation reason
last start/stop/success/failure
restart count
scheduled deadlines
event subscriptions
provided/consumed capabilities
resident/peak memory
CPU/runtime consumed
network transfer
power locks
open resources
quarantine state
```

This information is essential for diagnosing invisible background behavior and sleep failures.

---

## 47. Service Manager API direction

Core/internal APIs will likely need operations conceptually equivalent to:

```text
register/discover service
activate service
stop service
disable/enable service
quarantine service
deliver event
schedule deadline
cancel deadline
acquire capability
release capability
persist/restore state
query service status
```

Applications SHOULD interact with service-provided capabilities rather than controlling arbitrary service lifecycle directly unless a specific management permission exists.

---

## 48. Testing requirements

At minimum, tests SHOULD prove that:

1. a valid service package is discovered without loading its ELF;
2. incompatible/invalid service packages are rejected;
3. an event activates an unloaded subscribed service;
4. service work completes and ELF can unload afterward;
5. periodic work does not require a permanent task;
6. exact scheduled work survives sleep/wake;
7. durable alarm state survives service ELF eviction;
8. flexible work can be coalesced/deferred;
9. a service cannot directly obtain undeclared/unauthorized capabilities;
10. dependency resolution works by capability rather than ELF path;
11. unsatisfied dependencies leave a service waiting/degraded rather than crashing the platform;
12. resource ownership is reclaimed after normal stop;
13. resources are reclaimed after service failure where possible;
14. power locks are released on teardown;
15. a service cannot indefinitely block sleep outside authorized policy;
16. repeated failure triggers backoff/quarantine;
17. an optional broken service does not create a boot loop;
18. persistent service state restores after unload/reload;
19. state schema mismatch is handled explicitly;
20. memory pressure can unload inactive service ELFs;
21. service package replacement is atomic;
22. signed production policy rejects unauthorized service ELFs;
23. a valid signature does not grant undeclared system privilege;
24. module-type confusion between app/driver/service is rejected;
25. event queues remain bounded under event storms;
26. unloading removes/revokes module callbacks and mappings;
27. foreground work preempts/deprioritizes ordinary background work;
28. SD removal is handled according to service/package/state requirements.

---

## 49. Normative architectural rules

1. **Service is a distinct native ELF module class.**
2. **The Service Manager, not the service ELF, owns lifecycle and scheduling.**
3. **A service ELF does not imply a permanently running FreeRTOS task.**
4. **Services should normally load only when work exists and unload when idle.**
5. **Persistent deadlines belong to T5 core, not resident service tasks.**
6. **Exact alarms must survive ELF eviction and sleep.**
7. **Services subscribe to events declaratively where possible.**
8. **Unloaded services can be activated by registered events/deadlines/capability demand.**
9. **Services consume/provide capabilities rather than loading one another by path.**
10. **Cross-module communication should prefer handles/messages/events over long-lived raw callback pointers.**
11. **All service resources have framework-visible ownership.**
12. **Services have enforceable resource and wakeup budgets.**
13. **Loaded ELF residency alone never prevents sleep.**
14. **Repeated service failure must not create a permanent boot loop.**
15. **Production service ELFs are authenticated before execution.**
16. **Authentication does not imply unrestricted authorization.**
17. **Service state is separable from service ELF residency.**
18. **Large/cold service state follows the storage-backed memory architecture.**
19. **Core recovery machinery remains firmware-resident.**
20. **Service architecture must remain compatible with future process/memory isolation.**

---

## 50. Implementation sequence

Recommended implementation order:

1. define `service` as a first-class ELF/package module type;
2. define and version the service manifest schema;
3. implement service discovery/registry without ELF loading;
4. implement minimal Service Manager lifecycle and framework-owned execution context;
5. implement the core event bus with bounded queues and manifest subscriptions;
6. implement persistent deadline scheduling and power-manager integration;
7. define the minimal `t5_service_api_v1` and host ABI;
8. implement **time synchronization** as the first event/scheduled service;
9. add state persistence and ELF unload/reload behavior;
10. add resource accounting, runtime limits, power-lock ownership, and diagnostics;
11. add restart/backoff/quarantine policy;
12. extract the normal **background updater** into a service to validate periodic/flexible work and storage-backed state;
13. implement **alarm clock** service behavior to validate exact deadlines and deep-sleep wakeup;
14. integrate signed service packages with `SECURITY_ARCHITECTURE.md`;
15. add capability authorization and service-provided capability registration;
16. add memory-pressure eviction according to `MEMORY_ARCHITECTURE.md`;
17. only after these reference services work, migrate additional firmware background functionality into pluggable services.

The first milestone should prove the essential invariant with the time-sync service: **an ELF service can be discovered while unloaded, activated by an event, consume a framework capability, persist/schedule future work, release all resources, and disappear from memory without losing its logical system role.**
