# Shared-bus fault recovery: decision for R1

**October 2 disposition:** the [merged U1 handoff](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/82caa0997e913f01c1f5f9ab942d056bc9f04a82/docs/U1_OWNER_HANDOFF.md) records approval and implementation of manual-reboot retained-fault behavior. The decision request and alternatives below are **historical**, superseded by [E22](evidence.md#e22-integrated-bootstrap-is-a-firmware-and-package-contract-not-an-abi-rewrite). Do not ask the owner to select this policy again or edit closed PR96. Physical stall qualification remains separate, and retained failure may freeze the UI.

Historical analysis at Reader U1 `64d19d645720cba5c207f67c5d7fa1a031a07c0a`. This refines [R1](proposals.md#r1-prove-a-recoverable-boot-dependency-closure-and-exclusive-handoff), not U1–U4 requirements or implementation authorization. [E19–E20](evidence.md#e19-shared-spi-failure-boundary-and-current-u1-ownership) provide immutable source evidence and test limits.

## Owner decision prepared for the next handoff

**What:** decide whether an uncertain T5S3 shared SPI transfer may make **SD and LoRa unavailable until controlled reboot**, while the runtime retains the owning task and all potentially live resources. Prefer evaluating this narrow fail-closed contract over a new general recovery subsystem, but do not call it implemented or sufficient until the conditions below hold.

**Why:** SdFat's finite protocol timeout does not bound Arduino's lower mutex waits or controller polling. Timing out a caller cannot interrupt that work, release its mutex, or prove that buffers and mapped code are unused. LoRa shares the global SPI instance with SD; raw compatibility clients can bypass HalStorage. “Storage unavailable” alone is therefore an incomplete policy.

**Action:** the sole U1 owner needs the accepted outage/compatibility contract before choosing a port correction. If that contract is unsuitable, retain current behavior with explicit limited guarantees and leave bounded media I/O open; scope validated abort/recovery separately. The owner stopped the prospective patch instead of silently choosing this policy. This document makes no port change.

## Compare the retained and proposed designs

| Choice | Honest guarantee | Cost and decision |
| --- | --- | --- |
| A. Retain current behavior | Existing transaction/generation safeguards and finite upper-level work budgets remain useful; lower SPI progress and resumption after a controller fault are unproven. | Smallest change and compatibility baseline. Do not claim bounded media failure or recoverable runtime solely from an outer timeout. |
| B. Narrow existing-port fail-closed contract | After bounded detection, deny all affected access, retain uncertain ownership/resources, and recover through controlled reboot and ordinary startup recovery. | Conditional preferred investigation. Accept SD/LoRa outage, guarded legacy access, bounded retained memory and task lifetime. It must stop lower busy polling safely; abandoning a waiting caller is insufficient. |
| C. Validated abort and in-session recovery | Resume only after proven controller stop/reset, no live transfer, and coherent filesystem/cache and radio reinitialization. | Extra proof and scope. A reset register write or new mount generation alone does not establish safe reuse, durable data or restored LoRa. |

Existing cooperative-bounds and failed-quiescence contracts already require truthful failure and retained ownership. They do **not** select reboot-only recovery, authorize breaking compatible apps, or require a new controller-recovery subsystem. A narrowly justified correction of the existing module-store port can remain U1 if it preserves those boundaries. Full SPI ELF extraction and consumer migration retain their U3 order; U3 GUI remains compiled but optional, and U4 provisioning/image proof remains later.

## Affected clients and ownership boundary

The actual T5S3 set includes HalStorage-backed files; ordinary package transaction, inventory and loader paths; SD-backed app/module and firmware-source reads; legacy SDFS/FS/File operations; and raw SPIClass clients. NativeLoRaBridge constructs its radio Module with global SPI and explicitly prepares the shared SD/LoRa bus. Radio initialization, transfers and cleanup that touch that bus need the same fault policy. This is an inventory to verify against reachable exports and installed apps, not evidence that every compatibility entry is already mediated.

Raw-client uncertainty currently invalidates storage confidence; that observation alone cannot prevent another raw SDK call after a fault. Option B needs an effective admission boundary for **all** such calls, or explicit refusal of the affected app/import combination. Do not silently remove working compatibility apps or claim safety from HalStorage-only guards.

The EPD47 board's SD preparation also calls SPI.begin. That does **not** establish that e-paper pixel scanning uses this SPI path. Preserve expressive display buffers, overlap and accepted-flip semantics. Preserve shared-rail ownership too: a radio fault does not authorize powering down another consumer such as GNSS.

## Conditions for option B to qualify

- Bound parameter/controller lock acquisition and lower controller `cmd.update`/`cmd.usr` polling, with cooperative progress and a safe failure path. Include HalStorage locking: its current unavailable/generation helpers acquire the storage mutex, so a fault notification must not wait forever on the very lock whose owner is stuck.
- Distinguish failure **before acquisition** from uncertain work **after acquisition**. A non-owner cannot release the mutex. Retain the owning task/TCB and stack, transfer buffers, file/controller state, mapped provider code, candidates, dependencies and callbacks for as long as they may still be referenced. Do not delete the worker to manufacture cancellation.
- Park or otherwise safely retain the owner without continued unbounded polling. Charge retained resources to the target memory tier, cap uncertain work per affected controller, and reject new work before it can accumulate blocked workers. If safe bounded detection/retention is not possible, B fails; it is not an escape from the bounds requirement.
- Publish a fault state through a bounded path; deny subsequent SD and shared-radio/raw access, including cleanup calls that could reenter SPI. Late completion must not clear the fault or restore old receipts. Unrelated runtime/tool/UI functions remain usable only where their dependencies permit it.
- Reboot recovery uses ordinary transaction reconciliation, identity checks and new coherent storage generations. Preserve unknown user data and last-good state where the existing engine can prove it. “Reboot reachable” and “replacement recovered” are separate evidence, not a promise that any media corruption is repairable.

For C, additionally prove controller/DMA/interrupt inactivity before releasing resources; invalidate uncertain filesystem/cache views and receipts; remount only with a safe handle boundary; and restore radio state, interrupts and shared-resource ownership. Do not reuse an old File or provider merely because the controller accepts another transfer.

## Discriminating evidence, not a second implementation

The next authorized witness should distinguish lock contention before ownership, stuck update/start/transfer polling, late completion after timeout, and failure during close or publication. Check bounded caller behavior, cooperative scheduling, retained-owner lifetime, no repeated-worker growth, rejection of raw and radio access after fault, and ordinary recovery after restart. Include real SD-backed developer install/update/recover and representative daily app/radio flows once device testing is authorized. No physical test or daily T5S3 test is requested or claimed here.

Cardless host evidence now exercises the actual pinned LittleFS core/component and common transaction operations across **40 simulated program/erase interruption cases**. This favors retaining the manager. A 77-character transaction name is valid in that tested configuration; the earlier 64-character objection concerned disabled compatibility helpers. However, on a failed close the VFS can retain its descriptor after the core has removed file state and released an allocated cache. Blind retry/use is unsafe: track consumed core state separately from uncertain VFS ownership and durability. Do not retain a freed cache as if it remained live.

Those fixtures do not prove a complete cardless backend, installed-binary equivalence, physical flash timing/capacity/wear, or safe recovery from arbitrary instruction-level power cuts. LittleFS also has blocking locking boundaries to assess. The CAM read-only SDMMC bootstrap witness exercises a different transport from T5S3 Arduino SPI and cannot close this shared-bus proof gap. Keep the HTTP idle-body correction with its existing U1 worker; it is separate from this storage decision and does not call for a network rewrite.
