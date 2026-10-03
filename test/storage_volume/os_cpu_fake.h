#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
extern bool sd_mutex_fail_create, sd_mutex_fail_take, sd_mutex_fail_give;
extern void (*sd_mutex_before_give)(void);
extern unsigned sd_mutex_creates, sd_mutex_deletes, sd_mutex_takes, sd_mutex_gives;
void sd_mutex_set_context(bool isr, bool no_task, bool different_task);
#ifdef __cplusplus
}
#endif
