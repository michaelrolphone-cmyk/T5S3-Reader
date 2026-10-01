# U1 SD/SPI termination: owner decision pending

The source-grounded alternatives and lifetime conditions are recorded in the
[architecture recovery analysis at1486ce19](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1486ce199224a299982813ed1897096c76d504ad/docs/architecture-evolution/recovery-contract.md).
That analysis is not implementation authority or a merged U1 requirement.

The conditional recommendation is a reboot-required, fail-closed affected-bus
fault contract. On T5S3, SD and NativeLoRaBridge share global SPI. SD-backed
reading, package/app/module loading and native SDFS/File/SPIClass compatibility
clients therefore require the same policy. The existing bus owner, task/TCB,
mutex, possibly live buffers/handles and mapped code must remain retained; no
unproven abort/reset, unlock, destruction or reassignment is implied. Failed
acquisition must touch no bus, and sector retries must not refresh a whole-
operation deadline. A controller-recovery alternative requires proof of stop,
filesystem/cache reconciliation and shared-radio reinitialization.

No option is approved or implemented. The unmet contract remains bounded media
failure with ownership-safe cleanup and retained shared-bus/legacy compatibility.
The separate bounded loader, HTTP worker and sidecar corrections do not waive
that gate. Full U3 controller extraction remains outside this continuation.

**Implementation In Progress**
