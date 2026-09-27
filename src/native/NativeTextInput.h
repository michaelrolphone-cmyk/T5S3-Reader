#pragma once
#include <RiscTextInputV1.h>
#include <cstdint>

/* Firmware consumer of the transport-neutral input.text capability.
 * Lifecycle calls execute on the invocation/UI owner task. */
bool nativeTextInputBegin();
bool nativeTextInputActive();
bool nativeTextInputPoll();
int32_t nativeTextInputNext(risc_text_input_event_v1& out);
bool nativeTextInputEnd();
