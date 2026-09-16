# RiscRTE Application Capability Requirements — Initial Launch-Gating Implementation

Authority: [RiscRTE Platform Specification](RISCRTE_PLATFORM_SPEC.md), [Platform Capability Roadmap](PLATFORM_CAPABILITY_ROADMAP.md), [Runtime Driver Architecture](RUNTIME_DRIVER_ARCHITECTURE.md), [Application Execution Context Architecture](APPLICATION_EXECUTION_CONTEXT_ARCHITECTURE.md), and [Security Architecture](SECURITY_ARCHITECTURE.md). This document records the first implementation slice, not a replacement for the target dependency/authorization design.

## Manifest contract

Native application JSON sidecars MAY currently declare up to six mandatory `requires` and six `optional` capabilities. The normative target is that new app manifests declare dependencies; historical sidecars that omit these keys remain compatible until migrated. Each entry uses the target architecture's object syntax:

```json
{
  "requires": [{"capability": "location.position", "api": ">=1"}],
  "optional": [{"capability": "serial.port", "api": ">=1"}]
}
```

These keys supplement the existing display name, ELF filename, minimum firmware version, icon and version fields. Identifiers start with a lowercase ASCII letter, continue with lowercase letters, digits, period, underscore or hyphen, and occupy at most 39 bytes including no NUL. The initial supported API constraint is exactly `>=N`, with decimal N from 1 through 65535 and no leading zeros. Lists must be arrays of objects containing only `capability` and `api`; duplicate entries within or between lists, unknown constraint fields, invalid types, invalid ranges, and over-capacity lists are rejected. The whole sidecar must fit the existing 2048-byte runtime limit. Both the build validator (`scripts/app_manifest.py`) and firmware parser (`AppManifest.cpp`) validate declarations even when a legacy caller does not ask for the parsed requirements.

`optional` entries are validated but do not prevent launch. The runtime copies requirements into a bounded firmware structure and does not hold pointers into application code or raw manifest buffers. No existing `t5_app_manifest_t` or application ABI layout is changed.

## Pre-ELF runtime behavior

`launch_elf_app()` calls the firmware-only `native_app_capabilities_ready()` after establishing the SD VFS but **before** ELF symbol registration, `dlopen`, or application execution. The gate independently validates the matching sidecar, filename and minimum firmware version, reconciles existing USB host enumeration, and lazily publishes GNSS availability when a declared requirement asks for `location.position`. It does not start USB, claim UART, load a driver ELF, acquire a resource lease or invoke an app callback. An unresolved mandatory requirement returns `ESP_ERR_NOT_SUPPORTED` without mapping the ELF. Logs identify the missing capability, required API floor and reason. A later compatible launch remains possible. Loose legacy ELFs without sidecars remain supported by the existing file-browser path.

The first version-checked binding table is deliberately small: USB `usb.serial` implements `serial.port` through `T5SerialPortApi` v1, and onboard `gps-nmea` implements `location.position` through its existing `T5GpsApi` v1 compatibility facade. Only registry devices in `Available` state pass. A missing, unusable, unversioned or too-old provider fails closed. A record for an installed driver does not prove that it is activated. Other capability names, including higher-level location and sensor APIs, need registered provider API versions and activation before they can be mandatory launch requirements. This is an availability/version preflight, not a general dependency solver with provider activation, lease acquisition or configuration constraints.

### Security limits

**A manifest requirement is not a permission grant.** All hardware calls still need authenticated execution-context checks and the relevant provider's lease/ownership enforcement. This slice does not establish cryptographic authenticity of SD sidecars or ELF packages, does not isolate native code, and does not make the discovery inventory an authorization oracle. Adding a capability declaration alone must never grant arbitrary GPIO, USB, credential or device access. Consult the security architecture before widening any public acquire API.

### Validation and next implementation steps

`test/resources/app_capability_requirements_test.cpp` exercises parser helpers, capacity, duplicates, missing/offline/unknown-version/too-old providers and changed device generations under sanitizers. `test/native_apps/test_capability_manifest.py` validates build-side manifest behavior; the isolated ELF launcher test confirms rejected requirements cause zero `dlopen` operations and do not poison subsequent launches. CI must also build firmware for both supported boards and package the native apps/drivers.

Remaining before claiming full spec compliance: provider-owned API version metadata in the generic registry rather than the two known compatibility bindings; explicit requirement-resolution policy before springboard selection and after physical discovery; dependency activation and actual required handle acquisition before `dlopen`; trusted UI/permission grants; signed package and manifest verification; package identity; transport discovery that does not depend on existing host activation; human-readable launch errors in the UI; and physical USB/GNSS acceptance. Until those exist, do not add mandatory `serial.port` to an app expected to start and power/activate its own USB host, because this initial gate only sees interfaces that are already bound.
