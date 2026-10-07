#pragma once
// Private witness lifecycle seam, not a change to Storage/HalFile or a U1
// generation/receipt API. Only the mounting owner task may call this.
namespace BootstrapHalStorage {
bool releaseForHandoff();
unsigned openHandleCount();
}
