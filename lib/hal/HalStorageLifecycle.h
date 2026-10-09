#pragma once
// Board shutdown notification without exposing the filesystem dependency graph.
// Firmware-internal only; not part of the native application export surface.
void halStorageMediaUnavailable();

// Checked barrier before any board pin/rail changes; cancellation only before them.
bool halStoragePrepareForSleep();
bool halStorageCancelSleep();
