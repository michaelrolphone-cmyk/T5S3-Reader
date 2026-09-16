#include "GnssRecordAdapter.h"
#include "LocationPositionSubscriptions.h"
#include "CooperativeGnssProducer.h"

// Compile the provider-side wire format, bounded subscriber coordinator and
// cooperative pending-record state in both firmware targets. These are
// firmware-only until Device Registry authorization and the owner-task driver
// polling bridge are connected; compilation alone is not a live GPS feed.
static_assert(RuntimeStreams::GnssRecordAdapter::Size <= T5_STREAM_CHUNK,
              "GNSS record must fit one atomic stream transfer");
static_assert(RuntimeStreams::LocationPositionSubscriptions::MaxSubscribers <=
              RuntimeStreams::Registry::MaxStreams,
              "Location subscriptions must fit the shared stream registry");
