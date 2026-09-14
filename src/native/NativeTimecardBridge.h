#pragma once

#include <T5AppApi.h>

#include <cstddef>
#include <cstdint>

class GfxRenderer;
class MappedInputManager;

// Main-task bridge between native timecard apps and the firmware TimecardStore/UI.
// The bridge deliberately keeps persistence, date/time rules, translations, and
// theme rendering inside firmware so native and built-in Timecard use one model.
void nativeTimecardBegin(GfxRenderer& renderer, MappedInputManager& input);
void nativeTimecardEnd();

void nativeTimecardReload();
int32_t nativeTimecardTodayYmd();
int32_t nativeTimecardCurrentMinutes();
int32_t nativeTimecardSundayYmd(int32_t weekOffset);
int32_t nativeTimecardAddDays(int32_t ymd, int32_t days);
bool nativeTimecardGetDay(int32_t ymd, t5_app_timecard_day_t* day);
bool nativeTimecardSetPunch(int32_t ymd, uint8_t punch, int16_t minutesFromMidnight);
bool nativeTimecardPunchLabel(uint8_t punch, char* label, size_t capacity);
bool nativeTimecardFormatAmpm(int16_t minutesFromMidnight, char* text, size_t capacity);

void nativeTimecardRender(uint8_t screen, int32_t weekOffset, int32_t selectedIndex, int32_t editingYmd,
                          const char* status);
uint8_t nativeTimecardTouch(uint8_t screen, int16_t x, int16_t y, int32_t* selectedIndex);

// Editing uses the existing firmware keyboard during migration. The native app
// exits, the keyboard edits the shared TimecardStore, and the same ELF is resumed.
bool nativeTimecardRequestEdit(int32_t ymd, uint8_t punch, int32_t weekOffset);
bool nativeTimecardTakeResume(t5_app_timecard_resume_t* resume);
void nativeTimecardDispatchPendingAction(GfxRenderer& renderer, MappedInputManager& input, const char* resumePath);
