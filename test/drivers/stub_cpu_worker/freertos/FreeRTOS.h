#pragma once
#include <stdint.h>
#include <pthread.h>
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(m) pthread_mutex_lock(m)
#define portEXIT_CRITICAL(m) pthread_mutex_unlock(m)
#define configMAX_PRIORITIES 25
#define portNUM_PROCESSORS 2
#define tskNO_AFFINITY (-1)
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
int xPortInIsrContext(void);
