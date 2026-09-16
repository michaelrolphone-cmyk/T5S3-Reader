#pragma once
#include <T5StreamApi.h>

// Main task lifecycle. No allocation/tasks until an app requests the stream API.
void nativeStreamsBegin();
void nativeStreamsEnd();

// Runtime-provider helpers. These are firmware-internal and are not exported to
// application ELFs. The pair borrows the active USB serial service and exposes
// independently owned read/write stream handles for a semantic serial lease.
t5_stream_result_t nativeStreamOpenUsbPair(t5_stream_t* rx, t5_stream_t* tx);
t5_stream_result_t nativeStreamCloseOwned(t5_stream_t stream);
