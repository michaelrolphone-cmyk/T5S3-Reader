#include "GnssRecordAdapter.h"
#include "LocationPositionSubscriptions.h"
#include "CooperativeGnssProducer.h"
#include "LocationLeaseBinding.h"
#include "runtime/capabilities/DeviceRegistry.h"

// Build all lease-binding member functions against the identical DeviceRegistry
// implementation shared with PR #64. This checks the REAL compile-time
// contract on both firmware targets rather than only parsing a template or
// relying on an interface-matching host test double. Instantiation alone does
// not activate the GPS driver or export an ELF subscription getter.
template class RuntimeStreams::LocationLeaseBinding<
    RuntimeDevices::Registry, RuntimeDevices::LeaseInfo>;

static_assert(RuntimeStreams::GnssRecordAdapter::Size <= T5_STREAM_CHUNK,
              "GNSS record must fit one atomic stream transfer");
static_assert(RuntimeStreams::LocationPositionSubscriptions::MaxSubscribers <=
              RuntimeStreams::Registry::MaxStreams,
              "Location subscriptions must fit the shared stream registry");
