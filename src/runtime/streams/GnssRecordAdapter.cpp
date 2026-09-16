#include "GnssRecordAdapter.h"
#include "LocationPositionSubscriptions.h"

// Keep the provider-side record adapter and bounded subscriber coordinator in
// both firmware builds. They remain firmware-only until the Unified Device
// Registry's authorization and owner-task producer controls are connected.
static_assert(RuntimeStreams::GnssRecordAdapter::Size <= T5_STREAM_CHUNK,
              "GNSS record must fit one atomic stream transfer");
static_assert(RuntimeStreams::LocationPositionSubscriptions::MaxSubscribers <=
              RuntimeStreams::Registry::MaxStreams,
              "Location subscriptions must fit the shared stream registry");
