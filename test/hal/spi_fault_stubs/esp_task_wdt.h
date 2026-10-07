#pragma once
#define ESP_OK 0
int esp_task_wdt_status(void*);
int esp_task_wdt_delete(void*);

int esp_task_wdt_reset();
