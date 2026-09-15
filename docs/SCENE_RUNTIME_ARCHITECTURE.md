# T5S3 Scene Runtime Architecture

## Status

Architecture specification for the next generation of the T5S3 application runtime.

This document defines application ELF modules as **framework-managed scenes/controllers**, rather than independent processes or miniature firmware applications. It also defines navigation ownership, scene hydration, cached state, lifecycle, packaging, resource ownership, and the boundary between application scene ELFs and hardware driver ELFs.

The design is intentionally similar in responsibility to the scene/controller model used by UIKit storyboards: an installed application describes one or more scenes, the framework instantiates the requested scene, hydrates its state, and owns the view-controller-style navigation stack.

---

## 1. Goals

The scene runtime SHALL:

1. Make navigation a framework responsibility rather than an app-specific implementation.
2. Allow an installed application to contain one or more independently loadable ELF scene modules.
3. Preserve user-visible scene state while moving forward and backward through a navigation stack.
4. Permit inactive scene ELFs to be unloaded from PSRAM without losing their navigation state.
5. Provide deterministic Back behavior across all compliant applications.
6. Centralize framework UI including navigation bars, Back controls, scrolling primitives, fonts, display ownership, input dispatch, sleep behavior, and shared services.
7. Keep applications isolated from direct hardware implementation details.
8. Distinguish application scene modules from hardware capability driver modules.
9. Permit future applications to become substantially larger than the amount of executable memory available at any one time.
10. Preserve a simple single-scene application model for small utilities.

---

## 2. Architectural model

The runtime consists of four major layers:

```text
+---------------------------------------------------------+
|                    Installed Application                |
|  manifest + application data + one or more scene ELFs  |
+-----------------------------+---------------------------+
                              |
                              v
+---------------------------------------------------------+
|                       Scene Runtime                     |
| scene registry | navigation stack | state cache        |
| lifecycle      | hydration        | framework chrome   |
+-----------------------------+---------------------------+
                              |
                              v
+---------------------------------------------------------+
|                     T5 Framework APIs                   |
| UI | input | filesystem | network | power | capabilities|
+-----------------------------+---------------------------+
                              |
                              v
+---------------------------------------------------------+
| Firmware services + dynamically loaded driver providers |
+---------------------------------------------------------+
```

An ELF is therefore not inherently an "application." An ELF is a loadable implementation module.

For application UI, an ELF normally implements a **scene controller**.

For hardware support, an ELF may instead implement a **driver capability provider**.

These module classes have different ABIs, lifecycle rules, privileges, and ownership semantics.

---

## 3. Terminology

### Application package

An installed user-facing application consisting of a manifest, scene modules, application resources, and persistent application data.

### Scene

A navigable unit of application UI and behavior. A scene is identified by an application ID and scene ID.

### Scene controller

The executable controller implementation for a scene. In the current architecture this is normally an ELF module loaded through the native ELF loader.

### Scene descriptor

Framework-owned metadata sufficient to locate and instantiate a scene without loading it.

### Scene instance

A hydrated runtime instance of a scene controller.

### Navigation stack

An ordered framework-owned stack of scene entries representing the user's current navigation path.

### Scene state

Serializable, ephemeral state needed to restore the user's position in a scene, such as scroll position, selection, form contents, active tab, cursor position, or transient filters.

### Application data

Persistent domain data owned by an application. Application data is not the same thing as scene state and SHALL NOT depend on the navigation cache for durability.

### Framework chrome

UI owned by the runtime rather than the scene, including the navigation bar, Back control, title area, common status indicators, and other global controls.

### Driver provider

A dynamically loaded hardware/service module implementing a framework capability such as `position.gnss`. A driver provider is not a scene and does not participate in the navigation stack.

---

## 4. Core principle: framework-owned navigation

Applications SHALL NOT own the global navigation stack.

The framework SHALL own:

- Back behavior
- scene push/pop
- navigation-bar presentation
- transition lifecycle
- scene restoration
- controller loading and unloading
- scene cache eviction
- application exit behavior

A scene may request navigation, but it SHALL NOT directly manipulate another scene's ELF handle or controller pointer.

Conceptually:

```text
scene -> request push("detail", arguments)
framework -> resolve descriptor
framework -> suspend/cache current scene
framework -> instantiate detail scene
framework -> hydrate detail scene
framework -> present detail scene
```

The framework Back control SHALL invoke the navigation runtime, not an application-specific callback that independently decides where to go.

---

## 5. Navigation stack

A navigation entry SHALL remain valid even if its ELF is no longer resident.

A conceptual stack entry is:

```c
typedef struct {
    char app_id[T5_APP_ID_MAX];
    char scene_id[T5_SCENE_ID_MAX];
    char elf_path[T5_PATH_MAX];

    uint32_t scene_abi;
    uint32_t state_version;

    void *cached_state;
    size_t cached_state_size;

    uint32_t flags;
} t5_scene_entry_t;
```

The exact binary structure may differ, but the runtime SHALL preserve these semantics.

Example:

```text
Navigation stack

[0] App Store / catalog       cached, ELF unloaded
[1] App Store / detail        cached, ELF unloaded
[2] App Store / install       active, ELF resident
```

The user's navigation history therefore does not require every controller ELF to remain loaded.

---

## 6. Scene lifecycle

The scene ABI SHALL expose lifecycle operations equivalent to the following responsibilities:

```c
create
hydrate
will_appear
did_appear
will_disappear
serialize_state
destroy
```

A future ABI MAY add additional lifecycle events while preserving ABI versioning.

### Initial presentation

```text
load ELF
resolve scene ABI
create controller
hydrate initial arguments/state
will_appear
did_appear
```

### Push

```text
current.will_disappear()
serialize current state
optionally destroy/unload current controller
push new navigation entry
load new scene ELF
create controller
hydrate arguments or cached state
new.will_appear()
new.did_appear()
```

### Back / pop

```text
current.will_disappear()
serialize state if required
destroy current controller
unload current ELF
pop navigation entry
resolve previous entry
load previous ELF if not resident
create previous controller if necessary
hydrate previous cached state
previous.will_appear()
previous.did_appear()
```

### Application exit

The framework SHALL unwind or discard the application's scene stack according to application-exit policy, destroy active controllers, release scene-owned framework resources, and unload scene modules.

---

## 7. Hydration

Hydration restores a newly created controller to the logical state represented by its navigation entry.

A controller MUST NOT assume that returning to a previous scene means the same C/C++ object or ELF image still exists.

Hydration input may contain:

- navigation arguments
- serialized scene state
- framework restoration metadata
- application-level identifiers used to reload persistent data

Hydration SHALL NOT expose raw pointers retained from a previous ELF instance.

All state crossing an unload/reload boundary SHALL use a stable serialized representation.

---

## 8. Scene-state cache

Scene state is framework-managed ephemeral restoration data.

Examples include:

- list scroll offset
- selected item
- active tab
- current page
- partially entered form fields
- search query
- sort/filter selection
- cursor location
- expanded/collapsed rows

Scene state SHOULD be small.

Large content SHALL be stored as application data and referenced from scene state by stable identifiers.

The framework MAY keep scene state in RAM/PSRAM while memory permits and MAY spill eligible state to an SD-backed cache.

The framework MAY evict recoverable cached state under memory pressure according to policy. Applications requiring durable data SHALL write that data through persistent application storage rather than relying on the scene cache.

---

## 9. State versioning

Every serialized scene-state payload SHALL identify the scene state schema version.

A scene controller SHALL either:

1. accept and hydrate the supplied state version,
2. migrate a supported older state version, or
3. reject the cached state and initialize a clean scene.

A firmware or application update MUST NOT cause arbitrary stale state bytes to be interpreted as the current structure.

---

## 10. ELF residency and memory policy

Navigation state and ELF residency are independent concepts.

The runtime MAY choose among:

- keeping the previous ELF/controller resident for fast Back navigation,
- destroying the controller but retaining the loaded ELF,
- serializing state and fully unloading the ELF.

The policy MAY depend on:

- available internal RAM
- available PSRAM/executable mapping capacity
- scene memory cost
- navigation depth
- scene flags
- current system pressure

Correct application behavior SHALL NOT depend on a previous scene remaining resident.

This allows an application whose total code size exceeds executable memory to operate as a collection of individually loadable scenes.

---

## 11. Application package layout

A multi-scene application SHOULD use a package-oriented layout such as:

```text
/Apps/app-store/
    manifest.json
    scenes/
        catalog.elf
        detail.elf
        install.elf
    resources/
        ...
```

A simple application MAY contain only one scene.

The framework MAY retain compatibility with existing flat single-ELF applications during migration.

---

## 12. Application manifest

The application manifest SHALL describe the application independently of any one scene ELF.

Conceptual example:

```json
{
  "id": "app-store",
  "name": "App Store",
  "version": "1.0.0",
  "minimum_firmware": "1.1.0",
  "icon": "...",
  "initial_scene": "catalog",
  "scenes": {
    "catalog": {
      "elf": "scenes/catalog.elf"
    },
    "detail": {
      "elf": "scenes/detail.elf"
    },
    "install": {
      "elf": "scenes/install.elf"
    }
  }
}
```

The exact schema remains versioned separately, but the manifest SHALL provide enough information for the framework to resolve a scene ID without loading application code.

The app icon belongs to the application package and SHALL be used consistently by the launcher, home-screen shortcuts, App Store, and other framework surfaces.

---

## 13. Scene ABI

A scene ELF SHALL export a single versioned scene entry point or descriptor from which the runtime can obtain its controller operations.

Conceptually:

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

This example is normative in responsibility, not necessarily in final binary layout.

The host table SHALL expose framework services through versioned interfaces rather than allowing scene modules to link directly against firmware internals.

---

## 14. Navigation API exposed to scenes

Scenes SHALL request navigation through the framework.

Conceptual operations:

```c
push_scene(scene_id, args)
pop_scene()
pop_to_root()
present_scene(app_id, scene_id, args)
set_navigation_title(title)
set_navigation_options(options)
```

A scene SHALL NOT construct its own global Back stack.

A scene MAY implement local navigation inside its own content area, but framework-level Back semantics SHALL remain authoritative.

---

## 15. Framework UI ownership

The framework SHOULD provide common controls needed across applications, including:

- navigation bar
- Back control
- title rendering
- standard lists
- scroll containers
- dialogs
- menus
- common buttons
- font/icon lookup
- status overlays

Applications SHOULD compose these framework facilities rather than reproducing them independently.

This is required both for visual consistency and for correct integration with input, navigation, accessibility, power management, and future framework changes.

The App Store and other existing applications that implement their own navigation/chrome SHOULD migrate toward this model.

---

## 16. Input and activity

Input SHALL be dispatched by the framework to the active scene.

Only meaningful user input SHALL count as user activity for the global sleep timer unless explicitly specified otherwise by power policy.

Scene redraws, timers, polling loops, background work, or merely having an ELF loaded MUST NOT indefinitely reset the user's inactivity timer.

---

## 17. Power management

Scene execution does not imply a sleep lock.

An application that genuinely requires the device to remain awake SHALL request a framework power lock through an explicit capability and SHALL release it according to lifecycle rules.

When the configured Time to Sleep expires and no valid lock prevents sleep, the framework SHALL be able to suspend/cache the active scene and enter the configured sleep state.

This behavior SHALL remain consistent whether the user is on a firmware-native screen or inside an ELF scene.

---

## 18. Resource ownership

Framework resources acquired on behalf of a scene SHALL be associated with that scene instance where possible.

Resources include:

- timers
- subscriptions
- file handles exposed through framework wrappers
- capability leases
- power locks
- network operations
- temporary buffers

When a scene is destroyed, the framework SHOULD be capable of reclaiming resources that were not correctly released by the scene.

No scene SHALL retain callable pointers into another unloaded scene ELF.

---

## 19. Hardware access

Application scenes SHALL consume hardware through framework capabilities.

For example:

```text
GPS scene
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

The application scene SHALL NOT know the UART number, GPIO wiring, shared power rail implementation, or concrete GPS driver filename.

This maintains the hardware separation defined by the runtime driver architecture.

---

## 20. Scene ELFs versus driver ELFs

The two ELF classes SHALL remain explicitly distinct.

### Scene ELF

- user-facing controller
- participates in navigation lifecycle
- receives framework UI/input APIs
- can be hydrated from scene state
- can be unloaded while its navigation entry remains
- requests capabilities rather than owning hardware

### Driver ELF

- implements a hardware/service capability
- does not participate in the navigation stack
- receives narrowly scoped kernel I/O primitives
- lifecycle is controlled by capability acquisition/provider policy
- may own a hardware resource while active
- is installed and versioned independently

A manifest or ELF metadata field SHALL identify module type so the runtime cannot accidentally load a driver as a scene or a scene as a driver.

---

## 21. Cross-application navigation

The runtime MAY support navigation from one installed application into a scene exported by another application.

Such navigation SHALL use `(app_id, scene_id)` rather than a raw ELF path.

This allows the framework to validate installation state, firmware requirements, permissions/capabilities, and manifest compatibility before loading code.

Raw ELF paths SHOULD remain an internal loader concern.

---

## 22. Failure handling

If a scene cannot be loaded, resolved, created, or hydrated, the framework SHALL retain control of the navigation stack.

The framework SHALL NOT leave a partially pushed entry as the active scene.

For a failed push, the previous scene SHOULD be restored.

For a failed Back restoration, the runtime SHOULD continue unwinding toward the nearest recoverable scene or application root and present a framework error UI.

A malformed scene MUST NOT permanently break global Back navigation.

---

## 23. Crash containment

Full memory/process isolation is not provided merely by ELF loading on the ESP32-S3. Nevertheless, the runtime SHOULD establish containment boundaries where practical:

- validate ELF structure before load
- validate ABI versions
- bound state payload sizes
- reject invalid manifest paths
- invalidate controller pointers immediately after unload
- centralize resource ownership
- detect lifecycle misuse in debug builds
- restore navigation to a known framework screen after recoverable module failure

The architecture MUST NOT imply process-level isolation that the platform does not provide.

---

## 24. Back behavior specification

Back SHALL have one authoritative implementation.

When Back is invoked:

1. If the active scene has a framework-recognized modal/presentation layer, dismiss according to framework policy.
2. Otherwise, if the current navigation stack contains a previous entry, pop the active scene and restore the previous scene.
3. Otherwise, exit the application to the framework screen that launched it.

A scene MAY be notified that it is about to disappear, but it SHALL NOT be able to silently replace global Back semantics with a no-op.

This rule applies equally to hardware buttons, touch controls, and the framework navigation-bar Back control.

---

## 25. Home screen and launcher identity

Home-screen shortcuts SHALL reference an application ID and optional initial scene/arguments, not copy a generic ELF identity.

The framework SHALL resolve the current application manifest when rendering the shortcut.

Therefore application name and icon remain consistent after application updates and different applications do not collapse onto a shared ELF/default icon.

---

## 26. Compatibility with existing applications

Migration SHOULD be incremental.

### Phase 1: single-scene adapter

Existing one-ELF applications are treated as applications containing one implicit root scene. The framework owns entry/exit and Back behavior around them.

### Phase 2: framework navigation

Existing applications migrate custom navigation bars, Back behavior, scrolling containers, and shared controls to framework APIs.

### Phase 3: explicit scene ABI

Applications expose the versioned scene lifecycle and state serialization interface.

### Phase 4: multi-scene packages

Larger applications split logical screens into independently loadable scene ELFs where useful.

No application is required to split into multiple ELFs solely to conform to the architecture.

---

## 27. Example: App Store

The App Store is a natural multi-scene candidate:

```text
App Store

catalog.elf
    framework list/scroll container
    select app -> push_scene("detail", app_id)

detail.elf
    app metadata
    Back -> framework pop
    Install -> push_scene("install", app_id)

install.elf
    install/update progress
    completion state
```

If the user navigates:

```text
Catalog -> Detail -> Install
```

only `install.elf` may need to remain resident.

The stack can retain serialized Catalog and Detail state. Back from Install reloads/hydrates Detail; Back again reloads/hydrates Catalog at the same scroll position and selection.

---

## 28. Example: state hydration

Suppose the catalog scene had:

```text
scroll offset: 1840
selected application: usb-serial
filter: installed + available
```

Before unloading, the scene serializes this state.

When returning, the runtime:

1. loads `catalog.elf`,
2. creates a new catalog controller,
3. supplies the cached state,
4. the controller rebuilds its UI from current application data,
5. reapplies the filter, selection, and scroll position,
6. becomes visible.

The controller object itself did not survive. The user's scene did.

---

## 29. Testing requirements

The runtime test suite SHOULD cover at least:

- push/pop across two scenes
- three-or-more-level navigation
- Back from root exits correctly
- unload/reload between navigation entries
- state serialization and hydration
- stale state-version rejection
- malformed state rejection
- scene load failure during push
- scene load failure during Back restoration
- controller destruction
- resource reclamation
- navigation-bar Back and hardware Back equivalence
- sleep timeout while a scene is active
- explicit power lock behavior
- app icon resolution through the package manifest
- large scrollable framework lists
- repeated push/pop without PSRAM leakage
- scene ABI mismatch
- attempts to load driver ELF as scene
- attempts to load scene ELF as driver

---

## 30. Implementation direction

The recommended implementation order is:

1. Introduce a framework-owned navigation stack independent of the current activity implementation.
2. Make existing single-ELF apps appear as implicit root scenes.
3. Route all framework Back controls through that stack.
4. Define `t5_scene_api_v1` and its host interface.
5. Add bounded state serialization/hydration.
6. Add ELF unload/reload restoration tests.
7. Move common navigation bar and scrolling behavior into framework controls.
8. Extend app manifests with explicit scene declarations.
9. Add multi-scene package support.
10. Migrate larger applications such as App Store and Settings.

The architecture should be implemented without weakening the hardware boundary established by the runtime driver architecture.

---

## 31. Architectural invariant

The central invariant is:

> **The navigation stack represents user state; ELF residency is only an implementation detail.**

A user moves through application scenes. The framework owns that movement, retains enough state to reconstruct previous scenes, and dynamically loads the controller code needed for the scene currently being presented.

This allows the T5S3 firmware to provide a coherent application environment while keeping executable memory bounded and hardware ownership centralized.
