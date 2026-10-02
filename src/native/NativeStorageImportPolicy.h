#pragma once
#include <cstring>
// Exact temporary ABI entries with potential writable/remount authority.
// Read-only libc /sd VFS imports are not in this list. The caller additionally
// proves that resolution came from the registered compatibility symbol table.
// open/openNextFile may return writable handles; File's vtable contains indirect
// mutators even when the ELF does not name write/flush itself.
inline bool nativeRawStorageImport(const char* name) {
  if (!name) return false;
  constexpr const char* writable[] = {"SD",
                                      "_ZN2fs2FS4openEPKcS2_b",
                                      "_ZN2fs2FS6removeEPKc",
                                      "_ZN2fs2FS6renameEPKcS2_",
                                      "_ZN2fs4File12openNextFileEPKc",
                                      "_ZN2fs4File5closeEv",
                                      "_ZN2fs4File5flushEv",
                                      "_ZN2fs4File5writeEPKhj",
                                      "_ZTVN2fs4FileE",
                                      "_ZN2fs4SDFS3endEv",
                                      "_ZN2fs4SDFS5beginEhR8SPIClassjPKchb"};
  for (const char* symbol : writable)
    if (!std::strcmp(name, symbol)) return true;
  return false;
}
