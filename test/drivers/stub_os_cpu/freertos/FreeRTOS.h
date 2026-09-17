#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef void *TaskHandle_t;
typedef unsigned int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0u
#define taskENTER_CRITICAL(mux) ((void)(mux))
#define taskEXIT_CRITICAL(mux) ((void)(mux))
