# T5S3 Scene Runtime and Application Bundle Architecture

## Status

Architecture specification for the next generation of the T5S3 application runtime.

The fundamental application unit is an **application bundle containing a declarative directed scene graph**. Application ELF files are executable scene-controller modules within that graph; they are not independent applications. The framework owns graph resolution, the user's navigation stack, scene hydration, cached state, lifecycle, framework chrome, input, power behavior, and resource cleanup.

The model is intentionally similar in responsibility to UIKit's storyboard/navigation-controller architecture: the bundle declares the possible scene topology, controllers implement scene behavior, and the runtime records and restores the actual path traversed by the user.

> **Core invariant:** The application bundle defines the possible scene graph. The navigation stack records the user's actual path through that graph. ELF residency is only an implementation detail.

---

## 1. Goals

The runtime SHALL:

1. Treat an installed app as a bundle, not as a single ELF.
2. Require each bundle to declare a versioned directed graph of scenes and transitions.
3. Allow each scene to be implemented by an independently loadable ELF controller.
4. Make navigation and Back framework responsibilities.
5. Preserve scene state while users move forward and backward through the graph.
6. Permit inactive scene ELFs to be unloaded without losing navigation state.
7. Centralize navigation bars, Back controls, scrolling primitives, fonts/icons, display ownership, input dispatch, sleep behavior, and shared services.
8. Keep scenes isolated from direct hardware implementation details.
9. Distinguish scene-controller ELFs from driver-provider ELFs.
10. Permit applications larger than available executable memory by loading only required scene modules.
11. Support existing single-ELF apps during migration by representing them as one-node bundles.

---

## 2. Architectural model

```text
+----------------------------------------------------------------+
|                     Installed Application Bundle                |
| manifest + scene graph + scene ELFs + resources + app data     |
+-------------------------------+--------------------------------+
                                |
                                v
+----------------------------------------------------------------+
|                         Scene Runtime                           |
| graph resolver | navigation stack | hydration | state cache    |
| lifecycle      | ELF residency    | framework chrome           |
+-------------------------------+--------------------------------+
                                |
                                v
+----------------------------------------------------------------+
|                       T5 Framework APIs                         |
| UI | input | filesystem | network | power | capabilities       |
+-------------------------------+--------------------------------+
                                |
                                v
+----------------------------------------------------------------+
| Firmware services + dynamically loaded driver capability ELFs  |
+----------------------------------------------------------------+
```

An ELF is a loadable implementation module. For application UI it normally implements one scene controller. For hardware support it may implement a driver capability provider. These module classes have separate ABIs and lifecycle rules.

---

## 3. Terminology

### Application bundle
An installed user-facing app consisting of a manifest, directed scene graph, scene-controller modules, resources, and persistent app data.

### Scene graph
The bundle-declared directed graph `G = (V, E)` where vertices are scenes and edges are legal transitions. The graph defines **possible topology**, not current navigation history.

### Scene
A navigable unit of UI and behavior identified by `(app_id, scene_id)`.

### Scene controller
The executable implementation of a scene, normally an ELF loaded on demand.

### Transition
A named directed edge from one scene to another, with navigation semantics and optional argument/capability requirements.

### Navigation stack
The framework-owned ordered history of scene instances actually traversed by the user. It is a runtime path through the scene graph and may revisit the same graph node multiple times with different state.

### Scene state
Serializable ephemeral state required to reconstruct a scene instance: scroll position, selection, form contents, active tab, cursor, filters, and similar UI state.

### Application data
Durable domain data owned by an app. It SHALL NOT depend on the scene-state cache for persistence.

### Framework chrome
Runtime-owned UI such as the navigation bar, Back control, title, common status indicators, and global controls.

### Driver provider
A dynamically loaded hardware/service module implementing a framework capability. It is not part of an application's scene graph.

---

## 4. Bundle is the application boundary

The launcher, home screen, App Store, permissions, versioning, installation, update, and removal SHALL operate on **application bundles**.

A scene ELF SHALL NOT independently present itself as an installed app unless it is wrapped by a compatibility one-node bundle.

Application identity, display name, icon, version, minimum firmware, permissions/capabilities, initial scene, and graph belong to the bundle manifest.

Home-screen shortcuts SHALL reference an application ID plus an optional scene/deep-link and arguments. They SHALL NOT copy or infer identity from a generic ELF loader.

---

## 5. Bundle layout

Recommended SD-card layout:

```text
/Apps/app-store/
    manifest.json
    scenes/
        catalog.elf
        detail.elf
        install.elf
        settings.elf
    resources/
        ...
    data/
        ...
```

`data/` is conceptual; the framework MAY place writable app data in a managed location instead of inside the installed bundle. Installed executable/resources SHOULD be treated as package content and application data SHOULD survive a bundle update when compatible.

---

## 6. Scene graph

Every native bundle SHALL declare at least one scene and exactly one default entry scene.

Example topology:

```text
                     +----------+
                +--->| Settings |
                |    +----------+
                |
+---------+     |    +--------+      +---------+
| Catalog |-----+--->| Detail |----->| Install |
+---------+          +--------+      +---------+
     ^                    |
     +--------------------+
          Back/pop stack
```

The graph describes which forward transitions are valid. **Back is normally not encoded as a reverse graph edge.** Back pops the runtime navigation stack, because the scene being returned to is a prior scene *instance* with cached state, not merely a destination node.

A graph MAY contain cycles. The same scene MAY appear multiple times in a navigation stack with different arguments and state.

---

## 7. Manifest and graph schema

Conceptual versioned manifest:

```json
{
  "schema": 2,
  "id": "app-store",
  "name": "App Store",
  "version": "1.0.0",
  "minimum_firmware": "1.2.0",
  "icon": "fa-store",
  "initial_scene": "catalog",
  "scenes": {
    "catalog": {
      "controller": "scenes/catalog.elf",
      "title": "App Store"
    },
    "detail": {
      "controller": "scenes/detail.elf",
      "title": "Details"
    },
    "install": {
      "controller": "scenes/install.elf",
      "title": "Install"
    },
    "settings": {
      "controller": "scenes/settings.elf",
      "title": "Settings"
    }
  },
  "transitions": [
    {
      "id": "open-detail",
      "from": "catalog",
      "to": "detail",
      "mode": "push",
      "arguments": "app-reference-v1"
    },
    {
      "id": "install",
      "from": "detail",
      "to": "install",
      "mode": "push",
      "arguments": "app-reference-v1"
    },
    {
      "id": "settings",
      "from": "catalog",
      "to": "settings",
      "mode": "push"
    }
  ]
}
```

The final JSON schema SHALL be versioned. Unknown required schema versions SHALL be rejected before code is loaded.

---

## 8. Transition semantics

Each transition SHOULD be addressable by a stable transition ID. A transition descriptor MAY specify:

- source scene
- destination scene
- navigation mode
- argument schema/version
- required framework capabilities
- presentation metadata
- restoration policy
- optional guard/predicate identifier supported by the framework

Initial modes:

- `push` — push destination onto the current stack.
- `replace` — replace the active stack entry.
- `modal` — framework-managed presentation above the active scene.
- `reset` — establish destination as a new application root.

Scenes SHOULD request a transition by ID rather than hard-coding ELF paths. The runtime SHALL validate the requested edge against the active scene and graph before loading the destination.

---

## 9. Graph versus navigation stack

These structures MUST remain distinct.

```text
Declared graph:     Catalog -> Detail -> Install
                       |
                       +------> Settings

Runtime stack:      [Catalog A] [Detail B] [Install C]

ELF residency:       unloaded    unloaded    resident
```

The graph is static package topology. The stack is dynamic user history. ELF residency is a memory-management decision.

A navigation entry SHALL remain valid when its controller and ELF are no longer resident.

Conceptually:

```c
typedef struct {
    char app_id[T5_APP_ID_MAX];
    char scene_id[T5_SCENE_ID_MAX];
    char transition_id[T5_TRANSITION_ID_MAX];
    uint32_t scene_abi;
    uint32_t state_version;
    void *cached_state;
    size_t cached_state_size;
    uint32_t flags;
} t5_scene_entry_t;
```

The exact binary structure may differ; these semantics are normative.

---

## 10. Framework-owned navigation

Applications SHALL NOT own the global navigation stack.

The framework owns:

- graph parsing and validation
- transition resolution
- Back behavior
- push/pop/replace/modal/reset
- navigation-bar presentation
- transition lifecycle
- hydration/restoration
- controller loading/unloading
- cache eviction
- app entry/exit

A scene may request navigation but SHALL NOT manipulate another scene's ELF handle or controller pointer.

Conceptually:

```text
scene -> perform_transition("open-detail", args)
framework -> validate edge from active scene
framework -> serialize/suspend current scene
framework -> push navigation entry
framework -> resolve destination controller
framework -> load/create/hydrate destination
framework -> present destination
```

---

## 11. Back behavior

Back SHALL have one authoritative framework implementation.

1. Dismiss a framework-managed modal layer if one is active.
2. Otherwise, if the stack has a previous entry, serialize/destroy the active scene as policy requires, pop it, and restore the previous scene instance.
3. Otherwise exit the application to the framework surface that launched it.

A scene MAY receive lifecycle notification but SHALL NOT replace global Back with a no-op.

Because Back follows actual user history, graph edges do not need reciprocal Back edges.

---

## 12. Scene lifecycle

The scene ABI SHALL expose responsibilities equivalent to:

```text
create
hydrate
will_appear
did_appear
will_disappear
serialize_state
destroy
```

Initial presentation:

```text
resolve graph entry
load ELF
resolve scene ABI
create controller
hydrate arguments/restored state
will_appear
did_appear
```

Push:

```text
current.will_disappear()
serialize current state
optionally destroy/unload current controller
push destination entry
load/create destination
hydrate arguments/state
destination.will_appear()
destination.did_appear()
```

Pop:

```text
current.will_disappear()
serialize/destroy current
unload current ELF when appropriate
pop current entry
load previous ELF if needed
create previous controller if needed
hydrate previous cached state
previous.will_appear()
previous.did_appear()
```

---

## 13. Hydration and cached state

A controller MUST NOT assume that returning to a scene means the same C/C++ object or ELF image still exists.

All state crossing an unload/reload boundary SHALL use a stable serialized representation. Hydration may contain navigation arguments, serialized scene state, framework restoration metadata, and stable application-data identifiers. Raw pointers from a previous scene instance SHALL NOT be persisted.

Scene state SHOULD be small. Large/durable content SHALL be application data referenced by stable identifiers.

The runtime MAY keep state in RAM/PSRAM and MAY spill eligible state to SD-backed cache. Every state payload SHALL identify its schema version. A controller SHALL accept, migrate, or reject/reset incompatible cached state safely.

---

## 14. ELF residency policy

Correct application behavior SHALL NOT depend on inactive scenes remaining resident.

The runtime MAY:

- keep controller + ELF resident,
- destroy the controller while retaining the ELF,
- serialize state and fully unload the ELF.

Policy may depend on internal RAM, PSRAM/executable mapping capacity, scene cost, stack depth, and system pressure.

This permits a bundle's total executable code to exceed available executable memory.

---

## 15. Scene ABI

A scene ELF SHALL expose a versioned descriptor/API. Conceptually:

```c
typedef struct {
    uint32_t abi_version;
    const char *scene_id;
    int  (*create)(const t5_scene_host_v1 *host, void **controller);
    int  (*hydrate)(void *controller, const void *state, size_t size,
                    uint32_t state_version);
    void (*will_appear)(void *controller);
    void (*did_appear)(void *controller);
    void (*will_disappear)(void *controller);
    int  (*serialize_state)(void *controller, t5_state_writer_v1 *writer);
    void (*destroy)(void *controller);
} t5_scene_api_v1;
```

This is normative in responsibility, not final binary layout. Framework services SHALL be exposed through versioned host interfaces rather than direct linkage to firmware internals.

---

## 16. Navigation API exposed to scenes

Preferred API:

```c
perform_transition(transition_id, args)
pop_scene()
pop_to_root()
set_navigation_title(title)
set_navigation_options(options)
```

A lower-level `push_scene(scene_id, args)` MAY exist for framework/internal use, but application controllers SHOULD navigate through declared transitions so graph validation remains authoritative.

Cross-application navigation SHALL use an application ID plus an exported scene/deep-link identifier, never a raw ELF path.

---

## 17. Framework UI ownership

The framework SHOULD provide common controls including navigation bar, Back control, title rendering, standard lists, scroll containers, dialogs, menus, common buttons, font/icon lookup, and status overlays.

Apps SHOULD compose these facilities rather than reproducing them. This is required for consistent input, Back behavior, accessibility, power management, and framework evolution.

---

## 18. Input, activity, and power

Input SHALL be dispatched by the framework to the active scene. Only meaningful user input SHALL count as global activity unless power policy explicitly says otherwise.

Scene redraws, timers, polling, background work, or merely having an ELF loaded MUST NOT indefinitely reset the inactivity timer.

Scene execution does not imply a sleep lock. A scene that genuinely requires wakefulness SHALL explicitly acquire a framework power lock and release it according to lifecycle rules. When Time to Sleep expires and no valid lock prevents sleep, the runtime SHALL be able to serialize/suspend the active scene and enter sleep.

---

## 19. Resource ownership

Framework resources acquired for a scene SHOULD be associated with that scene instance: timers, subscriptions, wrapped file handles, capability leases, power locks, network operations, and temporary buffers.

When a scene is destroyed, the framework SHOULD reclaim scene-owned resources that were not released correctly. No scene SHALL retain callable pointers into another unloaded scene ELF.

---

## 20. Hardware access and driver ELFs

Application scenes consume hardware through capabilities:

```text
scene controller
      |
      v
position.gnss capability
      |
      v
gps-nmea driver ELF
      |
      v
kernel-owned UART/power primitives
```

Scenes SHALL NOT know UART numbers, GPIO wiring, shared power-rail details, or concrete driver filenames.

Scene ELFs participate in graph/navigation lifecycle. Driver ELFs implement hardware/service capabilities and do not participate in the scene graph or navigation stack. Module metadata SHALL distinguish these classes.

---

## 21. Bundle installation and validation

Before a bundle becomes launchable, the framework SHALL validate at minimum:

- manifest/schema version
- unique application ID
- unique scene IDs
- exactly one valid initial scene
- every transition source/destination exists
- every referenced controller path remains inside the bundle
- controller files exist
- minimum firmware/ABI compatibility
- declared module type

The runtime SHOULD additionally support package integrity hashes/signatures as the distribution model matures.

A malformed graph SHALL fail installation/registration rather than fail during navigation whenever validation can detect the problem earlier.

---

## 22. Deep links and exported scenes

A bundle MAY expose selected scenes or named routes as external entry points. Internal scene IDs need not automatically be externally callable.

An external launch SHALL be resolved through bundle metadata, validated, and used to create a new framework-owned navigation root or presentation according to policy.

This allows home-screen shortcuts and future inter-app workflows without exposing ELF paths.

---

## 23. Failure handling

If transition validation, ELF loading, ABI resolution, creation, or hydration fails, the framework SHALL retain control of the stack.

A failed push SHALL NOT leave a partially active entry. The previous scene SHOULD be restored. If Back restoration fails, the runtime SHOULD continue unwinding toward the nearest recoverable entry or application root and present framework error UI.

Full process isolation is not provided by ELF loading on ESP32-S3. The runtime SHOULD still validate ELF structure/ABI, bound state payloads, reject path traversal, invalidate pointers after unload, centralize resource ownership, and detect lifecycle misuse in debug builds.

---

## 24. Compatibility with existing applications

Existing flat one-ELF applications SHALL be represented as compatibility bundles containing one implicit root scene and no forward graph edges.

Migration phases:

### Phase 1 — implicit one-node bundles
Existing ELF apps launch through the Scene Runtime; framework owns entry, exit, and Back.

### Phase 2 — framework chrome and lifecycle
Apps migrate custom navigation bars, Back handling, scrolling, sleep/activity handling, and common controls to framework APIs.

### Phase 3 — explicit bundles and graphs
Apps gain `manifest.json`, named scenes, transitions, scene lifecycle, and state serialization.

### Phase 4 — split controllers
Large apps split logical scenes into independently loadable ELF controllers where memory or modularity benefits justify it.

A bundle with one scene remains valid; the graph model does not force unnecessary ELF splitting.

---

## 25. Build and distribution model

An application build SHOULD produce one installable bundle artifact containing manifest, graph declaration, scene controller ELFs, and resources.

The release system SHALL treat the bundle as the versioned application artifact. Individual scene ELFs are implementation details and SHOULD NOT normally be installed or updated independently of their owning bundle.

A future App Store/Bundle Manager SHOULD install, validate, update, and remove bundles atomically.

---

## 26. Testing requirements

The runtime test matrix SHALL include:

1. graph validation rejects missing nodes and invalid edges;
2. initial scene launches correctly;
3. declared transition pushes destination;
4. undeclared transition is rejected;
5. Back restores the exact prior scene instance/state;
6. cyclic graph navigation behaves correctly;
7. the same scene can appear multiple times in a stack with distinct state;
8. previous ELF can be unloaded and rehydrated on Back;
9. state-version mismatch fails safely;
10. failed destination load restores previous scene;
11. framework Back cannot be replaced with an app no-op;
12. inactivity sleep works while a scene ELF is active;
13. scene resource cleanup occurs on destroy;
14. home-screen shortcut resolves bundle icon/identity correctly;
15. driver ELF cannot be loaded as a scene and vice versa;
16. one-node compatibility bundles continue to work.

---

## 27. Architectural rules

1. **The app is the bundle, not the ELF.**
2. **The bundle declares the possible scene graph.**
3. **The navigation stack records the user's actual path through that graph.**
4. **Back follows stack history, not inferred reverse graph edges.**
5. **Scenes request declared transitions; the framework performs navigation.**
6. **Scene state survives controller/ELF eviction.**
7. **ELF residency is a memory-management detail, not navigation state.**
8. **Framework chrome, input, Back, sleep, and common UI remain framework-owned.**
9. **Application scenes consume capabilities; they do not own hardware.**
10. **Driver ELFs are capability providers and never scene-graph nodes.**
11. **Bundle installation/update is atomic at the application level.**
12. **A one-scene bundle is valid and is the compatibility model for legacy apps.**

---

## 28. Implementation direction

The next implementation milestone SHOULD establish the bundle/graph layer before aggressively splitting existing apps into multiple ELFs:

1. define the versioned bundle manifest and scene-graph schema;
2. add bundle discovery/registration under `/Apps/`;
3. represent existing ELF apps as one-node bundles;
4. implement the framework navigation stack and Back semantics;
5. add scene lifecycle and serialization/hydration;
6. route navigation through declared transition IDs;
7. migrate one multi-screen app (preferably App Store) into a true multi-scene bundle;
8. add ELF eviction/reload after behavior is correct with resident controllers;
9. add Bundle Manager/App Store installation after the package format stabilizes.

This sequence establishes the semantic architecture first and keeps executable-memory optimization from dictating application behavior.