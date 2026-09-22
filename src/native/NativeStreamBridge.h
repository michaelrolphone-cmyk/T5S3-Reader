#pragma once
#include <T5StreamApi.h>
#include <T5GpsApi.h>
#include <cstdint>
#include "runtime/streams/LiveGnssSession.h"

// Main task lifecycle. No allocation/tasks until an app requests the stream API.
void nativeStreamsBegin();
void nativeStreamsEnd();

// Runtime-provider helpers. These are firmware-internal and are not exported to
// application ELFs. The semantic serial pair is buffer-backed; provider I/O is
// pumped outside the registry mutex through NativeSerialPortBridge's generic
// session hooks. No transport-specific API or ELF pointer is stored in streams.
bool nativeStreamSerialIsBusy();
t5_stream_result_t nativeStreamOpenSerialPair(t5_stream_t* rx, t5_stream_t* tx);
t5_stream_result_t nativeStreamCloseOwned(t5_stream_t stream);
#if !defined(ESP_PLATFORM) && !defined(ARDUINO_ARCH_ESP32)
bool nativeStreamUsbIsBusy(); // host-fixture compatibility only
t5_stream_result_t nativeStreamOpenUsbPair(t5_stream_t* rx, t5_stream_t* tx);
#endif

// GNSS uses the VERY SAME stream registry/mutex as public streams and pipes.
// Only the claiming driver task may attach/poll/publish/stop. Caller-supplied
// owner IDs alone never authenticate an ELF; native bridge checks context.
// The source lease is borrowed and remains owned by GpsDriverRuntime.
t5_stream_result_t nativeGnssAttach(uint32_t owner, uint32_t device, uint32_t borrowedSourceLease);
RuntimeStreams::LiveGnssSession::PollDecision nativeGnssBeforePoll(uint32_t owner);
t5_stream_result_t nativeGnssPublishCopy(uint32_t owner, const t5_gps_state_t& state, uint32_t sampleMs);
void nativeGnssDisconnect(uint32_t owner);
// The consent handle is bound to the resulting stream under the stream mutex.
// Raw record reads must revalidate it; generic pipe delegation is disallowed.
t5_stream_result_t nativeGnssSubscribe(uint32_t authenticatedOwner, uint32_t authorizedDevice,
                                      uint32_t issuedReadConsent,
                                      uint64_t* subscription, t5_stream_t* stream);
t5_stream_result_t nativeGnssUnsubscribe(uint32_t authenticatedOwner, uint64_t subscription);
