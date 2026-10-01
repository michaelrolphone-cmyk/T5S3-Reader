# Qi charging with a USB controller

The T5S3 power profile now opts into externally powered USB host operation.
The owner's receiver LED remains lit on Qi; the published board schematic
connects the connector's VBUS to the BQ25896 input. This supports trying host
**data** operation on that existing rail. It does not prove receiver enumeration,
Qi current capacity, or immunity to charging-pad interference.

The BQ25896 cannot enable battery boost with its input powered. The new path
never enables boost or disables charging: it obtains a separate external-power
lease after host PHY/client/DMA preparation. The existing chip owner enables
continuous ADC sampling, waits a bounded conversion interval with scheduler
yields, and admits only good input, source-off, no live power fault and measured
4.8–5.2V (100mV ADC steps). An older profile or power provider keeps its original
behavior. There is no new firmware hardware bridge or competing register owner.

The USB controller attempts external host discovery once per incoming-power
session. With no peripheral attached it returns to boot USB serial after two
idle seconds. It does not repeatedly interrupt a connected computer. Attach the
receiver before activating Qi/navigation; if the idle probe has already ended,
turn external-input navigation off/on (with USB apps closed), or remove and
replace the Qi source, to retry. Incoming voltage alone cannot identify a Qi
pad versus a computer on this board. A computer may therefore see one temporary
serial disconnect during the initial host trial.

While active, every 500ms the controller checks power through its provider.
Power loss, invalid voltage/readback or a source-to-external change forces the
host receive detector disconnected. Ordinary detach events retire class claims
and DMA before releasing the old lease and selecting the next power mode.
Failed cleanup retains ownership. At most three external attempts are allowed
without an observed input-power removal. A lost/unstable supply is not permission
to boost into an external source. Moving onto/off Qi can briefly reconnect the
controller; seamless USB continuity is not claimed.

## Packages and validation

Update these three independently released packages together:

- `usb-controller-esp32s3`: 0.1.18 → 0.1.19
- `board-power-t5s3-v2`: 0.1.5 → 0.1.6
- `t5s3-usb-power-profile`: 0.1.0 → 0.1.1

No firmware change or ripple PR is required. The additive power/profile suffixes
are size checked; the original APIs and IDs remain valid.

Host tests cover the production power provider's charging/boost preservation,
voltage/fault rejection, ADC-write rollback, legacy profiles, source isolation,
lease lifetime, role transitions, bounded passive discovery, serial handback,
retained cleanup and ordering at the actual power-acquisition callback.
Physical charging/controller operation still needs device observation.

Sources: [LILYGO schematic](https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO/blob/H752-01/hardware/T5%20E-paper%20S3%20Pro%20V1.0%2024-12-24.pdf),
[TI BQ25896 datasheet, sections 9.2.5 and 9.2.14](https://www.ti.com/lit/ds/symlink/bq25896.pdf).
