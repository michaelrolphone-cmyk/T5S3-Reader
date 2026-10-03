#include <T5VideoApi.h>
#pragma once
#include "RiscProviderV2.h"
#include "T5DisplayProviderV1.h"
#ifdef __cplusplus
extern "C" {
#endif
bool display_provider_start(const risc_provider_dependency_v1 *deps,size_t count);
bool display_provider_quiesce(void);
// Internal callback after checked stop; invalidates portable frame tokens too.
void display_output_fast_stopped(void);
extern const t5_display_quality_api_v1 display_quality_dispatch;
extern const t5_video_api_v1 display_fast_dispatch;
#ifdef __cplusplus
}
#endif
