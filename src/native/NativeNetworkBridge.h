#pragma once

// Native HTTP compatibility state is scoped to one loaded application.
// Begin clears any prior cookie state; End releases it so cookies can never
// cross application execution contexts.
void nativeNetworkBegin();
void nativeNetworkEnd();
