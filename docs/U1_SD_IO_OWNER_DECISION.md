# U1 SD/SPI termination: approved manual-reboot policy

The owner approved reboot-required fail-closed SD/shared-LoRa stall behavior on
October1. The earlier alternatives remain documented in the
[architecture analysis at1486ce19](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1486ce199224a299982813ed1897096c76d504ad/docs/architecture-evolution/recovery-contract.md).
That analysis did not itself authorize implementation; the later explicit owner
approval did. It does not authorize automatic reboot or controller recovery.

The connected implementation and exact limits are in
[U1_SD_SPI_REBOOT_POLICY.md](U1_SD_SPI_REBOOT_POLICY.md). Existing owners,
mutexes, task stacks, possibly live buffers/handles and mapped code are retained;
new SD/shared-bus LoRa work refuses or remains parked until manual reboot.
No unproven abort/reset, unlock, destruction or ownership reassignment is used.
Failure to acquire a bus mutex cannot proceed into controller reset/transfer;
sector retries share one enclosing operation deadline.

Validated controller recovery remains a separate future choice requiring proof
of stop, filesystem/cache reconciliation and shared-radio reinitialization.
Full U3 controller extraction is outside this continuation. The newly implemented policy passed exact-head host and target checks at
1d93550f (runs 36915387635 / 36915387656); physical results are not implied.

**Implementation In Progress**
