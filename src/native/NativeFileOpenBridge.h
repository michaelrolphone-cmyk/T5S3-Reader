#pragma once

#include <string>

namespace NativeFileOpenBridge {

// During a file-association handoff, expose the selected SD file using the
// storage-relative path form expected by HalStorage ("/dir/file.ext").
// Returns false outside an active file-open handoff.
bool activeSourceStoragePath(std::string& out);

}  // namespace NativeFileOpenBridge
