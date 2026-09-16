#include "GnssRecordAdapter.h"
#include "LocationPositionSubscriptions.h"
#include "CooperativeGnssProducer.h"
#include "LocationLeaseBinding.h"

// Compile the GNSS wire format, subscriber coordinator, cooperative producer
// and lease-binding declarations on both targets. PR #64 owns the actual
// Device Registry; its concrete template instantiation and driver hooks are
// a subsequent cross-branch integration, not claimed by this compile check.
static_assert(RuntimeStreams::GnssRecordAdapter::Size <= T5_STREAM_CHUNK,
              "GNSS record must fit one atomic stream transfer");
static_assert(RuntimeStreams::LocationPositionSubscriptions::MaxSubscribers <=
              RuntimeStreams::Registry::MaxStreams,
              "Location subscriptions must fit the shared stream registry");
