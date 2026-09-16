#include "GnssRecordAdapter.h"

// Keep the provider-side wire adapter in both firmware builds even before a
// device-registry subscription is available. The implementation remains
// header-only; compiling this translation unit validates the target compiler's
// floating-point representation and record bounds rather than claiming a live
// GNSS data feed.
static_assert(RuntimeStreams::GnssRecordAdapter::Size <= T5_STREAM_CHUNK,
              "GNSS record must fit one atomic stream transfer");
