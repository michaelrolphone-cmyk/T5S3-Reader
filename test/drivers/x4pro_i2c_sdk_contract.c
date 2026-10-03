/* Compile with the actual pinned target SDK. Incompatible declarations fail
 * before linking: the provider's six existing ABI1 imports must stay exact. */
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "../../Drivers/x4pro_i2c/os_cpu_v1.h"
_Static_assert(sizeof(BaseType_t) == 4, "CPU ABI1 BaseType_t");
_Static_assert(sizeof(TickType_t) == 4, "CPU ABI1 TickType_t");
_Static_assert(queueQUEUE_TYPE_MUTEX == 1, "CPU ABI1 mutex kind");
_Static_assert(queueSEND_TO_BACK == 0 && pdTRUE == 1, "CPU ABI1 queue operation");
