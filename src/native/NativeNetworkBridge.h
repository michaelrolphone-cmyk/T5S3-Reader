#pragma once

// Native HTTP compatibility state is scoped to one loaded application.
// Begin clears any prior cookie state; End releases it so cookies can never
// cross application execution contexts.
void nativeNetworkBegin();
void nativeNetworkEnd();

// Trusted firmware composition only, serialized on the provider-owner task.
// Select an explicit installed network.default provider before native Begin.
// No Arduino shutdown, automatic device choice, reconnect or fallback occurs.
// The provider must reject conflicting physical ownership. Not an ELF import.
bool nativeNetworkBindInstalledLink(const char* providerId);
// Retry checked cleanup after a failed bind/request/End. Exact grant and
// activity remain in firmware custody until true; no app pointers are retained.
// Cleanup does not deselect installed mode inside an active native session.
bool nativeNetworkReleaseInstalledLink();
