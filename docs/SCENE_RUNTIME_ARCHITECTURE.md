# T5S3 State-Driven Scene Runtime and Application Bundle Architecture

## Status

Architecture specification for the next generation of the T5S3 application runtime.

The fundamental application unit is an **application bundle containing a declarative directed scene graph**. The runtime representation of a running application is **state**, principally a serializable navigation path plus application/model state. Application ELF files are disposable executable scene-controller modules used to materialize the current state into UI; they are not applications and are not the authoritative representation of navigation.

This combines the useful topology of a Storyboard-style scene graph with a modern state-driven navigation model: the bundle describes what the application is and where it can go, the navigation path describes where the user is, application/model state describes the durable or shared data being presented, and controller ELFs are loaded as needed to materialize scenes.

> **Core invariant:** The bundle graph describes where the user may go. The navigation path and state describe where the user is. Controller objects, rendered views, and ELF residency are disposable implementation details.

---

## 1. Fundamental model

The runtime SHALL treat these as separate concepts:

```text
Application Bundle
    = what the application IS

Scene Graph
    = where the application CAN GO

Application / Model State
    = durable and shared data the application KNOWS

Navigation Path + Scene State
    = where the user IS

Scene Controller ELF
    = executable code temporarily used to MATERIALIZE that state

Rendered View
    = disposable UI produced for the current state
```

This separation is normative. Correct application behavior MUST NOT depend on a particular controller object, view hierarchy, or ELF mapping remaining alive.

---

## 2. Goals

The runtime SHALL:

1. Treat an installed app as a bundle, not a single ELF.
2. Require each bundle to declare a versioned directed graph of scenes and transitions.
3. Make a serializable `NavigationPath` a first-class framework object.
4. Represent each navigation entry primarily as data, not as a resident controller.
5. Separate navigation/scene state from persistent application/model state.
6. Allow scene controllers and rendered views to be destroyed and reconstructed from state.
7. Make navigation and Back framework responsibilities.
8. Permit inactive scene ELFs to be unloaded without losing the user's place.
9. Permit the complete navigation path to survive sleep, restart, or memory reclamation when restoration policy allows.
10. Centralize navigation bars, Back controls, scrolling primitives, fonts/icons, display ownership, input dispatch, sleep behavior, and shared services.
11. Keep scenes isolated from direct hardware implementation details.
12. Distinguish scene-controller ELFs from driver-provider ELFs.
13. Permit applications larger than available executable memory by loading only required controller modules.
14. Support existing single-ELF apps during migration as one-node compatibility bundles.

---

## 3. Architectural model

```text
+----------------------------------------------------------------+
|                     Installed Application Bundle                |
| manifest | scene graph | scene ELFs | resources                |
+-------------------------------+--------------------------------+
                                |
                                v
+----------------------------------------------------------------+
|                    State-Driven Application Runtime             |
| NavigationPath | Scene State | Application/Model State         |
| graph resolver | restoration | lifecycle | framework chrome    |
+-------------------------------+--------------------------------+
                                |
                                | materialize active scene
                                v
+----------------------------------------------------------------+
|                    Disposable Scene Controller                  |
|               loaded from scene controller ELF                 |
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

An ELF is a loadable implementation module. For application UI it normally implements one scene controller. For hardware it may implement a driver capability provider. These module classes have separate ABIs and lifecycle rules.

---

## 4. Terminology

### Application bundle
An installed user-facing app consisting of a manifest, directed scene graph, scene-controller modules, and resources.

### Scene graph
The bundle-declared directed graph `G = (V, E)` where vertices are scenes and edges are legal transitions. It defines possible topology, not current navigation history.

### Scene
A logical navigable UI destination identified by `(app_id, scene_id)`.

### Scene controller
Disposable executable behavior used to materialize and interact with a scene, normally loaded from an ELF on demand.

### Rendered view
The current UI representation produced by a controller from state. It is not authoritative application state and MAY be discarded.

### Transition
A named directed graph edge from one scene to another with navigation semantics and optional argument/capability requirements.

### NavigationPath
A framework-owned, ordered, serializable value describing the user's actual navigation history. It contains scene identities, transition/launch arguments, restoration state, and version metadata sufficient to reconstruct the path without preserving controller objects.

### Navigation entry
One value in `NavigationPath`, representing one visit to a scene. Multiple entries MAY reference the same scene node with different arguments and state.

### Scene state
Small, serializable UI/restoration state associated with a navigation entry, such as scroll position, selection, active tab, editing state, cursor, or filters.

### Application/model state
Data whose lifetime is independent of any one rendered scene: configuration, domain records, downloads, installed-app information, current device/service state, and other shared or durable models.

### Framework chrome
Runtime-owned UI such as navigation bar, Back control, title, status indicators, and global controls.

### Driver provider
A dynamically loaded hardware/service module implementing a framework capability. It is not part of an application scene graph.

---

## 5. State is authoritative

The runtime SHALL regard state as authoritative and controllers/views as projections of that state.

The conceptual flow is:

```text
Application/Model State
          +
NavigationPath / Scene State
          |
          v
   resolve active scene
          |
          v
 load controller ELF if needed
          |
          v
 create + hydrate controller
          |
          v
     materialize UI
```

When state changes, the active controller MAY update the existing framework view tree efficiently. The architecture does not require a complete redraw for every state mutation.

However, the framework MUST be free to destroy the controller/view and later reproduce equivalent logical UI from the authoritative state.

A scene controller SHALL NOT use the existence of its own object as the only record of user-significant state.

---

## 6. Bundle is the application boundary

The launcher, home screen, App Store, permissions, versioning, installation, update, and removal SHALL operate on application bundles.

A scene ELF SHALL NOT independently present itself as an installed app unless wrapped by a compatibility one-node bundle.

Application identity, display name, icon, version, minimum firmware, permissions/capabilities, entry scene, restoration policy, and graph belong to the bundle manifest.

Home-screen shortcuts SHALL reference an application ID plus an optional exported route/scene and arguments. They SHALL NOT infer application identity from an ELF filename.

---

## 7. Bundle layout

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
```

Mutable application/model data SHOULD be kept in a framework-managed application data location so bundle replacement can be atomic without destroying compatible user data.

Runtime restoration state SHOULD likewise be framework-managed rather than written into immutable bundle content.

---

## 8. Scene graph

Every native bundle SHALL declare at least one scene and exactly one default entry scene.

```text
                     +----------+
                +--->| Settings |
                |    +----------+
                |
+---------+     |    +--------+      +---------+
| Catalog |-----+--->| Detail |----->| Install |
+---------+          +--------+      +---------+
```

The graph describes legal forward topology. Back is normally not represented as a reverse graph edge because Back operates on the actual `NavigationPath`.

A graph MAY contain cycles. The same scene MAY occur repeatedly in a path with different arguments and scene state.

---

## 9. Manifest and graph schema

Conceptual versioned manifest:

```json
{
  "schema": 3,
  "id": "app-store",
  "name": "App Store",
  "version": "1.0.0",
  "minimum_firmware": "1.2.0",
  "icon": "fa-store",
  "initial_scene": "catalog",
  "restoration": "navigation-path",
  "scenes": {
    "catalog": {
      "controller": "scenes/catalog.elf",
      "title": "App Store",
      "state_schema": 1
    },
    "detail": {
      "controller": "scenes/detail.elf",
      "title": "Details",
      "state_schema": 1
    },
    "install": {
      "controller": "scenes/install.elf",
      "title": "Install",
      "state_schema": 1
    },
    "settings": {
      "controller": "scenes/settings.elf",
      "title": "Settings",
      "state_schema": 1
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

The final JSON schema SHALL be versioned. Unknown required schema versions SHALL be rejected before controller code is loaded.

---

## 10. NavigationPath

`NavigationPath` SHALL be a first-class framework value, not merely an incidental collection of controller pointers.

Conceptually:

```c
typedef struct {
    char app_id[T5_APP_ID_MAX];
    char scene_id[T5_SCENE_ID_MAX];
    char transition_id[T5_TRANSITION_ID_MAX];

    uint32_t argument_schema;
    const void *arguments;
    size_t arguments_size;

    uint32_t state_schema;
    const void *scene_state;
    size_t scene_state_size;

    uint32_t flags;
} t5_navigation_entry_t;

typedef struct {
    uint32_t schema_version;
    t5_navigation_entry_t *entries;
    size_t count;
} t5_navigation_path_t;
```

This structure is conceptual; final ownership/allocation rules may differ.

A persisted representation MUST be self-contained and MUST NOT contain raw pointers. Scene/controller ELF paths SHOULD NOT be persisted in the path; `(app_id, scene_id)` is resolved through the currently installed bundle.

Example logical value:

```json
[
  {
    "scene": "catalog",
    "state": {
      "scroll": 1840,
      "filter": "available"
    }
  },
  {
    "scene": "detail",
    "arguments": {
      "app": "usb-serial"
    },
    "state": {
      "tab": "versions"
    }
  },
  {
    "scene": "install",
    "arguments": {
      "app": "usb-serial"
    }
  }
]
```

Only the active entry normally needs to be materialized as a controller/view.

---

## 11. Graph versus NavigationPath

These MUST remain distinct:

```text
Declared graph:      Catalog -> Detail -> Install
                        |
                        +------> Settings

NavigationPath:      [Catalog A] [Detail B] [Install C]

Controller residency: unloaded    unloaded    resident

Rendered view:                               Install UI
```

The graph is package topology. `NavigationPath` is user/navigation state. Controller residency is memory policy. The rendered view is presentation.

This separation permits the runtime to reconstruct the same user-visible navigation state with entirely new controller objects after memory reclamation or restart.

---

## 12. Navigation transitions

Each transition SHOULD have a stable transition ID. A descriptor MAY specify source, destination, mode, argument schema/version, required capabilities, presentation metadata, restoration policy, and framework-supported guards.

Initial modes:

- `push` — append destination entry to `NavigationPath`.
- `replace` — replace the active entry.
- `modal` — create a framework-managed modal presentation state associated with the active path.
- `reset` — replace the path with a new root destination.

Scenes SHOULD request transitions by ID rather than destination ELF path.

The framework SHALL validate the edge and arguments before committing the new navigation value.

Conceptually:

```text
controller requests perform_transition("open-detail", args)
        |
        v
framework validates graph edge + args
        |
        v
framework captures current scene state
        |
        v
framework mutates NavigationPath
        |
        v
framework materializes new active entry
```

Navigation is therefore fundamentally a state mutation followed by presentation.

---

## 13. Back behavior

Back SHALL have one authoritative framework implementation.

1. Dismiss framework-managed modal state if active.
2. Otherwise, if `NavigationPath` contains more than one entry, capture current state as required and remove the active entry.
3. Materialize the new final entry in the path.
4. If the path is at its application root, exit to the framework surface that launched the bundle.

Back follows the stored path, not inferred reverse graph edges.

A controller MAY receive lifecycle notification but SHALL NOT replace global Back with a no-op.

Hardware Back and framework navigation-bar Back SHALL mutate the same navigation state.

---

## 14. Scene state versus application/model state

These lifetimes SHALL be kept distinct.

### Scene state

Belongs to one navigation entry and exists to reconstruct the user's UI position. Examples:

- scroll offset
- selected row
- active tab
- expanded section
- current search/filter
- editing/cursor state
- temporary form input when restoration is appropriate

### Application/model state

Exists independently of a scene/controller and SHOULD be accessible through framework-managed application services/models. Examples:

- application settings
- installed app catalog
- download/install state
- saved documents
- persistent user choices
- domain records
- shared service state

Destroying a controller MUST NOT destroy application/model state.

Scene state SHOULD contain stable identifiers into model data rather than duplicate large models.

---

## 15. State observation and UI updates

The framework SHOULD evolve toward observable model/state interfaces so a controller can declare or subscribe to the data it depends on rather than continuously polling hardware or global state.

Conceptually:

```text
Model / State changes
        |
        v
framework notifies interested active scene
        |
        v
controller updates affected framework UI
```

This specification does not require a SwiftUI-style diff engine or value-type view DSL. The ESP32 implementation MAY remain imperative internally.

The architectural requirement is narrower and more important: **the durable truth lives outside the disposable rendered view/controller**.

A future declarative UI layer MAY therefore be added without changing the bundle, graph, navigation-path, or state-lifetime model.

---

## 16. Scene lifecycle

The scene ABI SHALL expose responsibilities equivalent to:

```text
create
hydrate
will_appear
did_appear
state_changed
will_disappear
serialize_state
destroy
```

`state_changed` MAY initially be implemented through narrower subscriptions/callbacks rather than one generic callback.

Initial materialization:

```text
resolve active NavigationPath entry
resolve graph node
load ELF
create controller
hydrate(arguments, scene state, model handles)
will_appear
materialize UI
did_appear
```

Forward transition:

```text
current.will_disappear()
serialize current scene state
commit NavigationPath mutation
optionally destroy/unload current controller
materialize new active entry
```

Back:

```text
current.will_disappear()
destroy/unload current as policy allows
pop NavigationPath
materialize new final entry from its stored state
```

The controller lifecycle follows navigation state; it does not define navigation state.

---

## 17. Hydration

Hydration reconstructs a controller/view from authoritative runtime state.

A controller MUST NOT assume returning to a scene means the same C/C++ object, view tree, or ELF image still exists.

Hydration MAY receive:

- transition/launch arguments
- serialized scene state
- scene-state schema version
- application/model handles or service interfaces
- framework restoration metadata

Raw pointers from a previous controller instance SHALL NOT be persisted.

A hydrated controller SHOULD produce logically equivalent UI even after the previous controller and view were completely destroyed.

---

## 18. Serialization and restoration

`NavigationPath` SHOULD be serializable when bundle policy permits restoration.

This enables restoration after:

- controller eviction
- executable-memory pressure
- normal device sleep
- firmware-controlled restart
- application suspension

The framework SHOULD persist a restoration envelope containing at least:

```text
bundle ID
bundle version compatibility information
NavigationPath schema version
ordered navigation entries
argument schema/version per entry
scene-state schema/version per entry
restoration timestamp/generation
```

Application/model state SHALL use its own persistence mechanisms and SHALL NOT be embedded wholesale into `NavigationPath`.

On restoration, the runtime SHALL validate the currently installed bundle and each referenced scene/state schema before materializing the active entry.

If a path is no longer compatible after an update, the framework MAY migrate it where supported, truncate it to the nearest recoverable entry, or return to the bundle's root scene.

---

## 19. State size and storage policy

Scene state SHOULD remain compact.

Large data SHALL be stored in application/model storage and referenced by stable identifiers.

The framework MAY keep navigation state in internal RAM/PSRAM while active and MAY serialize restoration data to SD or other persistent storage.

The runtime SHOULD impose explicit per-entry and per-path size limits to prevent a scene from turning navigation state into unbounded storage.

---

## 20. State versioning

Every serialized navigation path, argument payload, and scene-state payload SHALL identify its schema version.

A scene/controller SHALL either:

1. accept the supplied state version;
2. migrate a supported older version; or
3. reject it so the framework can reset/truncate restoration safely.

Updates MUST NOT cause stale arbitrary bytes to be interpreted as current state structures.

---

## 21. Controller and ELF residency policy

Correct application behavior SHALL NOT depend on inactive scenes remaining resident.

The runtime MAY:

- keep controller + view + ELF resident;
- destroy the view/controller while retaining the ELF mapping;
- serialize state and fully unload the ELF.

Policy MAY depend on internal RAM, PSRAM/executable mapping capacity, scene cost, path depth, foreground/background status, and system pressure.

Because `NavigationPath` is authoritative, controller eviction is cache eviction rather than navigation destruction.

This permits a bundle's total executable code to exceed available executable memory.

---

## 22. Scene ABI

A scene ELF SHALL expose a versioned descriptor/API. Conceptually:

```c
typedef struct {
    uint32_t abi_version;
    const char *scene_id;

    int  (*create)(const t5_scene_host_v1 *host, void **controller);

    int  (*hydrate)(void *controller,
                    const void *arguments,
                    size_t arguments_size,
                    uint32_t argument_schema,
                    const void *scene_state,
                    size_t scene_state_size,
                    uint32_t state_schema);

    void (*will_appear)(void *controller);
    void (*did_appear)(void *controller);
    void (*will_disappear)(void *controller);

    int  (*serialize_state)(void *controller,
                            t5_state_writer_v1 *writer);

    void (*destroy)(void *controller);
} t5_scene_api_v1;
```

This is normative in responsibility, not final binary layout.

Framework services and model/capability access SHALL be exposed through versioned host interfaces rather than direct linkage to firmware internals.

---

## 23. Navigation API exposed to scenes

Preferred API:

```c
perform_transition(transition_id, args)
pop_scene()
pop_to_root()
dismiss_modal()
set_navigation_title(title)
set_navigation_options(options)
request_scene_state_checkpoint()
```

A lower-level `push_scene(scene_id, args)` MAY exist internally, but application controllers SHOULD navigate through declared transitions so graph validation remains authoritative.

Controllers SHALL NOT construct their own global controller stack and SHALL NOT load destination ELFs directly.

---

## 24. Framework UI ownership

The framework SHOULD provide common controls including navigation bar, Back control, title rendering, standard lists, scroll containers, dialogs, menus, common buttons, font/icon lookup, and status overlays.

Apps SHOULD compose these facilities rather than reproducing them. This is required for consistent navigation, input, accessibility, power management, restoration, and framework evolution.

Framework UI controls SHOULD themselves be reconstructible from navigation and scene state where user-significant state exists.

---

## 25. Input, activity, and power

Input SHALL be dispatched by the framework to the active materialized scene.

Only meaningful user input SHALL count as global activity unless power policy explicitly states otherwise. Scene redraws, timers, observation callbacks, polling, background work, or merely having an ELF loaded MUST NOT indefinitely reset inactivity time.

Scene execution does not imply a sleep lock. A scene requiring wakefulness SHALL explicitly acquire a framework power lock and release it according to lifecycle rules.

Before sleep, the framework SHOULD checkpoint restorable navigation/scene state according to policy. On wake/restart, it MAY reconstruct the active scene from that state instead of preserving the original controller object.

---

## 26. Resource ownership

Framework resources acquired for a materialized scene SHOULD be associated with that scene instance: timers, subscriptions, wrapped file handles, capability leases, power locks, network operations, and temporary buffers.

When a controller is destroyed, the framework SHOULD reclaim scene-instance resources that were not correctly released.

Application/model services may outlive individual scene instances when their ownership is explicitly application- or framework-scoped.

No scene SHALL retain callable pointers into another unloaded scene ELF.

---

## 27. Hardware access and driver ELFs

Application scenes consume hardware through capabilities:

```text
Application / Model State
          ^
          |
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

Scene ELFs materialize application scenes. Driver ELFs implement capabilities. Driver ELFs do not participate in the scene graph or `NavigationPath`. Module metadata SHALL distinguish these classes.

---

## 28. Bundle installation and validation

Before a bundle becomes launchable, the framework SHALL validate at minimum:

- manifest/schema version
- unique application ID
- unique scene IDs
- exactly one valid initial scene
- every transition source/destination exists
- every controller path remains inside the bundle
- controller files exist
- minimum firmware/ABI compatibility
- declared module type
- restoration/state schema metadata where required

The runtime SHOULD additionally support package integrity hashes/signatures as distribution matures.

A malformed graph SHALL fail installation/registration rather than fail during navigation whenever possible.

---

## 29. Deep links and exported scenes

A bundle MAY expose selected scenes or named routes as external entry points. Internal scene IDs need not automatically be externally callable.

An external launch SHALL be resolved through bundle metadata and represented as framework-owned navigation state. It SHALL NOT expose raw ELF paths.

Home-screen shortcuts MAY therefore identify a bundle, exported route, and serialized launch arguments while allowing the bundle's actual controller implementation to change between versions.

---

## 30. Failure handling

If transition validation, controller loading, ABI resolution, creation, or hydration fails, the framework SHALL retain control of `NavigationPath`.

A failed forward transition SHALL NOT leave a partially committed destination active. The runtime SHOULD restore the prior path value.

If materializing a previous entry fails during Back/restoration, the runtime SHOULD truncate/unwind toward the nearest recoverable entry or application root and present framework error UI.

Because navigation truth exists outside controller objects, controller failure MUST NOT corrupt the framework's global navigation machinery.

Full process isolation is not provided by ELF loading on ESP32-S3. The runtime SHOULD still validate ELF structure/ABI, bound state payloads, reject path traversal, invalidate pointers after unload, centralize resource ownership, and detect lifecycle misuse in debug builds.

---

## 31. Compatibility with existing applications

Existing flat one-ELF applications SHALL be represented as compatibility bundles containing one implicit root scene and no forward graph edges.

Migration phases:

### Phase 1 — implicit one-node bundles
Existing ELF apps launch through the runtime; framework owns entry, exit, and Back.

### Phase 2 — framework chrome and lifecycle
Apps migrate custom navigation bars, Back handling, scrolling, sleep/activity handling, and common controls to framework APIs.

### Phase 3 — first-class NavigationPath and state
Navigation ceases to depend on resident activity/controller objects. Scene state becomes serializable and restorable.

### Phase 4 — explicit bundles and graphs
Apps gain manifests, named scenes, declared transitions, state schemas, and restoration policy.

### Phase 5 — split controllers
Large apps split logical scenes into independently loadable ELF controllers where memory or modularity benefits justify it.

A one-scene bundle remains valid; the graph/state model does not force unnecessary ELF splitting.

---

## 32. Build and distribution model

An application build SHOULD produce one installable bundle artifact containing manifest, graph declaration, controller ELFs, and resources.

The release system SHALL treat the bundle as the versioned application artifact. Individual scene ELFs are implementation details and SHOULD NOT normally be installed or updated independently of their bundle.

A future App Store/Bundle Manager SHOULD install, validate, update, and remove bundles atomically.

Application/model data and compatible restoration state SHOULD survive bundle replacement according to version/migration policy.

---

## 33. Testing requirements

The runtime test matrix SHALL include:

1. graph validation rejects missing nodes and invalid edges;
2. initial scene materializes correctly from an initial path;
3. declared transition mutates `NavigationPath` correctly;
4. undeclared transition is rejected without changing the path;
5. Back restores the exact prior navigation entry/state;
6. cyclic graph navigation behaves correctly;
7. the same scene can occur multiple times with distinct arguments/state;
8. inactive controller/view/ELF can be destroyed and reconstructed;
9. complete path can be serialized/deserialized;
10. path restoration after sleep/restart reconstructs the active scene;
11. incompatible path/state versions fail or truncate safely;
12. failed destination materialization restores the previous path;
13. framework Back cannot be replaced with an app no-op;
14. model state survives controller destruction;
15. scene state does not become the application's durable data store;
16. inactivity sleep works while a scene ELF is active;
17. scene resources are reclaimed on destroy;
18. home-screen shortcut resolves bundle identity correctly;
19. driver ELF cannot be loaded as a scene and vice versa;
20. one-node compatibility bundles continue to work;
21. controller eviction under memory pressure does not change navigation state;
22. restoration after a compatible bundle update resolves scenes through the new bundle rather than stale ELF paths.

---

## 34. Architectural rules

1. **The app is the bundle, not the ELF.**
2. **The bundle declares the possible scene graph.**
3. **`NavigationPath` is first-class serializable application-runtime state.**
4. **The navigation path records where the user is; it does not store controller identity as truth.**
5. **Application/model state is independent of scene/controller lifetime.**
6. **Scene state belongs to a navigation entry and remains compact.**
7. **Back mutates navigation state, then the runtime materializes the new active entry.**
8. **Scenes request declared transitions; the framework performs navigation.**
9. **Controllers and rendered views are disposable projections of state.**
10. **ELF residency is a memory-management detail, not navigation state.**
11. **Framework chrome, input, Back, sleep, and common UI remain framework-owned.**
12. **Application scenes consume capabilities; they do not own hardware.**
13. **Driver ELFs are capability providers and never scene-graph/navigation-path nodes.**
14. **Bundle installation/update is atomic at the application level.**
15. **A one-scene bundle is valid and is the compatibility model for legacy apps.**

---

## 35. Implementation direction

The next implementation milestone SHOULD establish state-driven navigation before aggressively splitting existing apps into many ELFs:

1. define a versioned serializable `NavigationPath` and navigation-entry representation;
2. separate scene state from application/model state in framework APIs;
3. make framework Back mutate `NavigationPath` rather than manipulate app-owned controller history;
4. implement materialization of the active navigation entry into the existing ELF/controller runtime;
5. represent existing ELF apps as one-node compatibility bundles;
6. define the versioned bundle manifest and scene-graph schema;
7. add bundle discovery/registration under `/Apps/`;
8. add state checkpoint, serialization, hydration, and restoration;
9. verify controller/view destruction and reconstruction before introducing aggressive ELF eviction;
10. route forward navigation through declared transition IDs;
11. migrate one multi-screen app, preferably App Store, into a true multi-scene state-driven bundle;
12. add memory-pressure ELF eviction;
13. add Bundle Manager/App Store installation after package format stabilizes;
14. add observable model/state interfaces incrementally without requiring a declarative UI rewrite.

This sequence makes navigation/state semantics authoritative first. ELF loading then becomes an optimization and execution mechanism beneath the application model rather than the application model itself.
