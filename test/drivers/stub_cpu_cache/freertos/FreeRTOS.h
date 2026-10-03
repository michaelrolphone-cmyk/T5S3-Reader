#pragma once
#include <stdbool.h>
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
int cache_try_lock(void);
void cache_unlock(void);
#define portTRY_ENTER_CRITICAL(lock,timeout) ((void)(lock),(void)(timeout),cache_try_lock())
#define portEXIT_CRITICAL(lock) ((void)(lock),cache_unlock())
int xPortInIsrContext(void);
