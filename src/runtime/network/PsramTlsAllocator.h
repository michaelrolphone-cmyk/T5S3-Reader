#pragma once

namespace RuntimeNetwork {

// Install once at startup, before any TLS client is constructed.
void enablePsramTlsAllocations();

}  // namespace RuntimeNetwork
