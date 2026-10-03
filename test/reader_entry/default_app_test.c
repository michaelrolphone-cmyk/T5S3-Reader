#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "T5ReaderEntryApi.h"

void app_main(void);
static t5_reader_entry_api_v1 table;
static int present, lookups, pumps, continue_count;
static uint32_t terminal;
static uint32_t pump(void) {
  ++pumps;
  assert(pumps <= continue_count + 1); // No work after terminal handoff/stop.
  return pumps <= continue_count ? T5_READER_ENTRY_CONTINUE : terminal;
}
const t5_reader_entry_api_v1* t5_reader_entry_get_api(uint32_t version) {
  assert(version == T5_READER_ENTRY_ABI_VERSION);
  ++lookups;
  return present ? &table : NULL;
}
static void reset(void) {
  table = (t5_reader_entry_api_v1){T5_READER_ENTRY_ABI_VERSION, sizeof(table), pump};
  present = 1; lookups = pumps = continue_count = 0;
  terminal = T5_READER_ENTRY_STOP;
}
int main(void) {
  reset(); present = 0; app_main(); assert(lookups == 1 && pumps == 0);
  reset(); ++table.abi_version; app_main(); assert(pumps == 0);
  reset(); table.struct_size = offsetof(t5_reader_entry_api_v1, pump); app_main(); assert(pumps == 0);
  reset(); table.pump = NULL; app_main(); assert(pumps == 0);
  reset(); table.struct_size = offsetof(t5_reader_entry_api_v1, pump) + sizeof(table.pump) - 1;
  app_main(); assert(pumps == 0);
  for (unsigned invocation = 0; invocation < 1000; ++invocation) {
    reset(); continue_count = invocation % 7;
    terminal = invocation % 2 ? T5_READER_ENTRY_HANDOFF : T5_READER_ENTRY_STOP;
    app_main(); assert(lookups == 1 && pumps == continue_count + 1);
  }
  reset(); table.struct_size += 64; continue_count = 2; app_main(); assert(pumps == 3);
  reset(); terminal = UINT32_MAX; app_main(); assert(pumps == 1); // Fail closed on unknown status.
  puts("Actual default.c: ABI admission, cooperative pump, handoff/stop, and repeated entry PASS");
}
