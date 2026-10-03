#pragma once
#include <T5DisplayProviderV1.h>
// Composition only. The installed ELF owns both physical display engines.
const t5_display_provider_api_v1* platformDisplayProvider();
