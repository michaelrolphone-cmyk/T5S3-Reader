#pragma once
// Firmware composition only. Every transport/filesystem operation stays in ELF.
// Early board composition uses the immutable flash store, never SD.
bool beginPlatformBoardProviders();
bool beginPlatformStorage();
bool drainPlatformProvidersForSleep();
