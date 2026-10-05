#include <stddef.h>
#include "T5ReaderEntryApi.h"

// PaperSpace is the existing Reader application. No second UI, menu, settings,
// storage model or renderer lives here. Returning lets the host unload this
// invocation BEFORE running a child; the next invocation resumes its activities.
__attribute__((visibility("default"))) void app_main(void) {
  const t5_reader_entry_api_v1* reader =
      t5_reader_entry_get_api(T5_READER_ENTRY_ABI_VERSION);
  if (!reader || reader->abi_version != T5_READER_ENTRY_ABI_VERSION ||
      reader->struct_size < offsetof(t5_reader_entry_api_v1, pump) + sizeof(reader->pump) ||
      !reader->pump) return;
  while (reader->pump() == T5_READER_ENTRY_CONTINUE) {}
}
