#include "RiscProviderV2.h"
#include "RiscUsbDiscoveryDiagnosticsV1.h"
/* The deployed master prefixes must remain binary compatible after U1 adds
 * stream binding, checked physical close, control and coherent snapshots. */
_Static_assert(offsetof(risc_driver_streams_v2, last_error) ==
               offsetof(risc_driver_diagnostics_v2, last_error), "diagnostic offset");
_Static_assert(offsetof(risc_driver_streams_v2, bind_streams) >=
               sizeof(risc_driver_diagnostics_v2), "stream suffix overlap");
_Static_assert(offsetof(risc_usb_host_discovery_v1, interrupt_read) ==
               offsetof(risc_usb_host_interrupt_v1, interrupt_read), "interrupt offset");
_Static_assert(offsetof(risc_usb_host_discovery_v1, diagnostic) ==
               offsetof(risc_usb_host_diagnostics_v1, diagnostic), "host diagnostic offset");
_Static_assert(offsetof(risc_usb_host_discovery_v1, release_checked) >=
               sizeof(risc_usb_host_diagnostics_v1), "checked release overlap");
int main(void) { return 0; }
