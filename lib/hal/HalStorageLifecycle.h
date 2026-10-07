#pragma once
// Board shutdown notification without exposing the filesystem dependency graph.
// Firmware-internal only; not part of the native application export surface.
void halStorageMediaUnavailable();
