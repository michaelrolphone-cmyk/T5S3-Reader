# Deployment provisioning manifest — U4 implementation contract

**Status: FUTURE / NOT YET IMPLEMENTED. Authorized implementation milestone: [U4 headless provisioning and ESP32-S3-CAM streaming](NEXT_MILESTONE_PROVISIONING_AND_ESP32_S3_CAM.md).** Deferred from U1, U2 and U3; not a U3 completion gate. U3 MUST already boot the actual ESP32-S3-CAM stably and headlessly with no board drivers or provisioning manifest and no graphical UI startup dependency. Read [Platform Specification](RISCRTE_PLATFORM_SPEC.md), [hardware boundary](HARDWARE_AGNOSTIC_DRIVER_BOUNDARY.md), [unified manager](UNIFIED_PACKAGE_MANAGER_MVP.md), [ordinary ZIP package format](RISC_PACKAGE_FORMAT.md), [U2 sources](NEXT_MILESTONE_INDEPENDENT_PACKAGE_ECOSYSTEM.md), [U3](NEXT_MILESTONE_T5_PRO_HARDWARE_DRIVER_MIGRATION.md), [U4](NEXT_MILESTONE_PROVISIONING_AND_ESP32_S3_CAM.md), and the binding [UI-optional/headless camera streaming contract](HEADLESS_RUNTIME_AND_CAMERA_SERVICE_BOUNDARY.md). U4 finalizes schema/transport and implements this design; examples here are not a deployed parser/API.

## 1. Goal and operating model

An owner prepares a supported provisioning medium or other actually available authorized recovery/staging input with bounded versioned desired state and optionally ordinary package ZIP archives. On eligible first boot or explicitly owner-approved reapply, a minimally bootstrapped runtime identifies the real board, validates compatibility, resolves dependencies, installs/configures real driver/provider/application/service packages through **the existing ordinary package manager**, activates required providers, and starts the **declared headless camera streaming service**. The graphical firmware UI may remain compiled in but is skipped on headless profiles. Neither graphical Settings, display/touch, foreground app nor interactive serial input may be required to install, run or recover the deployment. Serial output may provide only noninteractive diagnostics on a verified supported board/port.

Provisioning is a declarative initial-state operation, **not** a new installer, package format, continuously polling reconciler, firmware flasher, device-specific switch or capability authority. Once committed, normal manager, driver and service lifecycle own operational state. A changed SD/manifest never silently downgrades or reapplies a working deployment. Missing medium/manifest yields normal generic idle, not a fatal error. A functional network or SD interface is never presumed to exist to install its own missing driver. Preserve data and boot/recovery through failures.

## 2. Manifest and source contract

Finalize one bounded schema, discovery path(s) for actual supported bootstrap media, version grammar, maximum bytes/count/depth, normalized paths/URLs, conflicting revisions, source and archive pins/hashes, ownership and time/memory budgets. A board without SD is not provisionable via an invented slot: specify a verified alternate independent medium, such as owner-staged recovery/programming bundle. Distinguish port bootstrap from normal installed storage driver.

Manifest MUST cover:

- Stable deployment ID/revision, schema version, CPU/architecture, runtime/port/ABI/firmware compatibility and **verified actual board/profile identity**; self-declared board identity is insufficient. Reject wrong board before applying GPIO/power, sensor profile or peripheral activation.
- Selected required/optional hardware profiles, controller/bus/pin/power mapping supplied as typed data to **installed driver/provider ELFs only**. Core never interprets camera/LoRa/USB hardware maps. Boot/module-store prerequisites are distinct from drivers.
- All desired ordinary packages (`application`, `driver`, `service`, `provider`) with canonical IDs, exact version or constraints, source/immutable release, capability requirements, dependencies and architecture/ABI. Reuse the standard resolver, one install engine and one stream/ZIP path; a pinned package cannot silently switch source/version.
- Bundled verified archives and optional U2 GitHub release snapshot; check length, digest, manifest, actual ELF/import/ABI, runtime permissions and URL/path limits. Online fetch is optional and cannot bootstrap its own missing network or credentials.
- Allowlisted typed initial settings for authorized packages and port, optional UI/startup app only on a graphical profile, camera capture mode/rate/resolution bounds, selected actual streaming output transport/protocol/access policy, network configuration where applicable, and **explicit authorized desired service startup/autostart**. Installing an ELF, labeling it core or carrying a manifest never grants hardware rights or automatic execution.

Illustrative **non-executable** example (field names and actual board/sensor/media are not finalized or claims about user hardware):

```json
{
  "schema_version": 1,
  "deployment_id": "example-cam",
  "revision": 1,
  "target": {"platform": "riscrte", "board_profile": "verified-cam-profile", "firmware": ">=1.2.0"},
  "hardware_profiles": ["board/example-cam", "peripheral/verified-camera"],
  "packages": [
    {"kind": "driver", "id": "example-sensor", "version": "1.0.0"},
    {"kind": "service", "id": "example-camera-stream", "version": "1.0.0"}
  ],
  "package_source": {"mode": "verified-bootstrap-medium"},
  "services": [{"id": "example-camera-stream", "autostart": true}],
  "settings": {}
}
```

The stream service requires a reachable output capability and approved configuration not depicted by this incomplete example. The real board's sensor, network path and supported provisioning medium MUST be established at U4 kickoff; do not hard-code example data.

## 3. Bootstrap, owner authorization, idempotence and recovery

Small isolated generic port/recovery code can inspect actual board/port identity, read a verified independent provisioning source, initialize generic manager and report status. It must NOT expose a normal camera/display/SD/Wi-Fi/USB host production fallback. If media driver would need to load from itself, implement a bounded isolated bootstrap reader or truly independent recovery transfer and an explicit handoff without competing ownership. Missing manifest means idle and accessible recovery, not error loop. Serial/ROM/USB debug output is allowed only where the actual port supports it and does not count as an installed runtime USB host/provider.

**Do not require an interactive interface.** Any preview/consent must be achievable by inspecting the bounded plan through a verified owner-operated deployment/recovery process or pre-authorizing a specific deployment policy outside the headless device using the platform's real permissions. The device validates that authorization before privileged admission, camera startup, network exposure or unsafe power/pins; a manifest, hash, removable-media possession or package origin is not itself consent or publisher authentication. If valid authorization cannot be provided through the selected real bootstrap path, reject the sensitive operation with a clear noninteractive status and recoverable idle—not a UI modal or a hypothetical serial prompt. Do not silently enable a public/unrestricted camera endpoint.

U4 implements a bounded cooperative state machine:

1. Detect eligible first run/approved reapply and verify authentic board/port facts, media, manifest identity/revision and actual profile compatibility; no manifest means generic idle.
2. Compare desired and installed inventory and derive an inspectable source/version/dependency/permission/activation/**startup-service** plan. Reject wrong ABI/board, duplicate IDs, collisions, ambiguous providers, unavailable streaming transport, unsafe settings or unauthorized source migration before electrical activation.
3. Validate documented owner permission for privileged imports, camera operation, output access and auto-start; do not add signing, trust roots or pretend a self-declared hash is authorization. Do not expose credentials in portable manifests or logs; use a separately authorized secure setting mechanism.
4. Reuse ordinary manager preflight, immutable archive/manifest verification, streaming staging, transactional installation/recovery and normal provider lifecycle. Install first, then activate and apply safe allowlisted settings. Resolve `camera.*` and an actually reachable output/network transport; start the authorized service through UI-independent generic service manager. Distinguish installed, activated, running, listening and streaming instead of treating install as service success.
5. Persist bounded atomic ledger: deployment ID/revision/digest, source and versions, stage/results, authorized startup intent, service activation status, completion/failure and recovery checkpoints. Reboot or missing media resumes idempotently where possible; an identical complete manifest skips installation but RESTORES approved service autostart. Never assume multi-package global atomicity when only per-package commits are guaranteed.
6. Mark deployment complete only after installed inventory, capabilities, settings and authorized service startup/readiness match intent; actual sustained frames to an external client form U4 functional/physical proof. If transport/camera is unavailable, report a truthful degraded/failed service state and leave generic boot/recovery functional rather than pretending deployment succeeded.

On interruption, power loss, corrupt archive, media removal, network failure or revoked rights, preserve last-good packages, user data and safe device state. File/network/hash/graph/frame operations require explicit memory/count/time/retry caps, actual scheduler yields, progress and bounded cleanup; watchdog feed alone is not cooperation. Protect camera/transport permissions, frame data and credentials; a generic package's author/role is not access authority. Provisioning does not flash firmware, edit partitions or override bootloader policy.

## 4. Durable service operation after provisioning

Provisioning is not an always-running camera transport. The separate, independently packaged `service` supplies it. Persist owner-approved desired startup policy in ordinary service configuration, NOT as an ongoing boot-time installer, and let the generic UI-independent service supervisor restore it after normal restart. Capture frames via semantic camera ELF; stream bounded chunks or properly owned frame handles with source/time/size/format metadata to an independently reachable receiver using the verified chosen protocol. Apply explicit consumer authorization, source-revocation/purge, flow control, frame-size/rate/memory limits, cancellation/client disconnect/reconnect and safe teardown without raw pointers across ELF unload. Display or graphical app is never a dependency. A one-time still frame saved locally is a useful diagnostic but **not** camera streaming completion.

Serial output is only optional one-way boot/deployment/service diagnostics: selected profile, package installation state, camera availability, service status, client/errors/recovery. No interactive serial setup UI, shell or remote-control API is required by these milestones. If a network transport is selected, provide the minimal already-supported connectivity/configuration path actually necessary; do not assume provisioned Wi-Fi credentials or migrate intrinsic ESP32-S3 Wi-Fi/BLE into independent drivers solely to implement U4.

## 5. U4 acceptance; not U3 gate

During owner-initiated Release Qualification, verify the actual CAM and T5 with exact artifacts: authorized headless first-run deployment installs correct drivers/service; no GUI/interactive console; valid output/config; actual real camera probe and **successive frames received by another real client**; camera service starts after provisioning and automatically resumes on reboot without re-install; stop/revoke, driver removal, absent client/transport, wrong board/ABI, missing/corrupt medium and interrupted install/recovery are correctly handled. Pro UI/drivers still operate on correct profile. The identical firmware binary is a target only when both flash/PSRAM/boot constraints allow; record minimal port differences otherwise. U3's driverless headless boot must already be proven separately; U4 cannot require CAM packages simply to boot.

**Explicit deferral U1–U3:** generic package, port, profile, stream/service and recovery primitives only; no first-run manifest parser/ledger, CAM driver, streaming service or owner hardware-test request until U4. **Explicit U4 exclusion:** full firmware GUI-to-ELF conversion, interactive serial UI, general cloud/dashboard platform, new cryptographic package signature requirements and firmware flashing by provisioning. Owner advances and qualifies milestones explicitly; spec changes alone certify no software or hardware.
