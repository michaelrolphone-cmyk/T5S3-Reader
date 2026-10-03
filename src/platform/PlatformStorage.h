#pragma once
// Firmware composition only. Every transport/filesystem operation stays in ELF.
bool beginPlatformStorage();
bool drainPlatformProvidersForSleep();
