# Adding Legacy Firmware Activities to RiscRTE

> **Authority and scope:** Start with `RISCRTE_PLATFORM_SPEC.md`, `PLATFORM_CAPABILITY_ROADMAP.md`, and `SCENE_RUNTIME_ARCHITECTURE.md`. This guide documents the **legacy compiled-in Activity implementation path**. New user-facing functionality should normally be an independently packaged RiscRTE application using platform capabilities/services. Use this guide only when required functionality cannot yet be expressed through those APIs or when maintaining an existing firmware Activity.

The source-level classes and paths below retain historical names because they identify current implementation. They do not define the RiscRTE application architecture.

## Current compiled-in Activity model

Built-in firmware applications are C++ classes derived from `Activity` and managed by `ActivityManager`. Adding one means adding the activity, wiring its navigation factory, and updating the firmware Home routing. This path has direct access to firmware internals and therefore carries more coupling and firmware-update cost than a RiscRTE ELF application.

The existing Timecard and Ask activities are useful implementation references for Activity lifecycle, child activities, storage/network access, localization and rendering. Where equivalent functionality is available through RiscRTE host APIs, new applications should use the host API instead of copying these internals.

### Lifecycle requirements

Activities must follow `ActivityManager` ownership/navigation semantics, release resources on exit, and avoid retaining invalid child/activity pointers. Rendering must follow the framework render lock/session rules. Back navigation must pop the current activity rather than create an unrelated replacement. Power/sleep behavior remains system-owned.

### Home integration

When a firmware Activity genuinely requires a Home entry, keep Home labels, icons, counts, selection indices and activation routing synchronized. Prefer shared framework icon/rendering facilities rather than application-private controls. A firmware Activity should not create a second navigation bar, input mapping, network stack, storage layer, or other private platform facility.

### Child activities and system UI

Existing firmware Activities may invoke firmware-owned children such as keyboard, Wi-Fi selection and complex settings screens. RiscRTE ELF applications use the corresponding host/system-UI handoff APIs and unload/resume lifecycle instead of directly instantiating those C++ classes.

### Storage, networking and hardware

Even compiled-in Activities should move toward the platform boundary: use RiscRTE services/providers rather than direct hardware calls. Hardware-specific ESP-IDF/Arduino operations belong in providers/runtime-owned abstractions. Reusable HTTP, stream, storage, device and sensor behavior belongs in platform services/capabilities rather than the Activity.

## Preferred path: RiscRTE applications

For independently installable applications, read `NATIVE_APPS.md` (legacy filename; RiscRTE application framework). Applications are ELF packages with manifests and versioned host APIs. They can be built/released independently and should request semantic capabilities instead of depending on firmware internals.

Use `NATIVE_UI_API.md` for framework-owned presentation/navigation, `NATIVE_NETWORK_API.md` for the current network compatibility host API, and `STREAM_PIPE_ARCHITECTURE.md`/`STREAM_PIPE_MVP.md` for bounded data movement. As roadmap facilities become available, application manifests should declare capabilities/intents and the resolver should supply implementations.

## Migration rule

Do not add a compiled-in Activity merely because an older feature was implemented that way. First determine whether the feature can be expressed as a RiscRTE application, service, capability, stream/transform, device/provider, job or intent handler. If a missing platform primitive forces firmware coupling, implement the smallest reusable primitive consistent with the roadmap and keep application policy outside firmware where practical.

Existing `Activity`, `ActivityManager`, source paths and class names remain valid current-state references until migrated. They should not be renamed in documentation in a way that makes the code impossible to locate.