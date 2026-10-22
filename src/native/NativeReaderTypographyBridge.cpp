#include "NativeReaderTypography.h"
#include "CrossPointSettings.h"
#include "SdCardFontGlobals.h"
#include "activities/ActivityManager.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Keep existing firmware selection, SD fallback, and resource ownership here.
// Native apps hold the normal render lock throughout this synchronous call.
bool nativeReaderPage(const risc_reader_page_request_v1 *q,risc_reader_page_result_v1 *out) {
  if(out) *out={};
  if(!out || !nativeReaderPageRequestValid(q)) return false;
  const uint32_t started=millis();
  ensureSdFontLoaded();
  vTaskDelay(1);
  if(millis()-started>=5000u) return false;
  const NativeReaderTypographyResources resources{
      activityManager.nativeAppRenderer(),SETTINGS.getReaderFontId(),
      SETTINGS.getReaderLineCompression()};
  return nativeReaderPage(resources,q,out,started);
}
