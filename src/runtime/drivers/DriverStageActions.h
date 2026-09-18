#pragma once

#include "runtime/packages/PackageOrdinaryStageRecovery.h"

namespace RuntimeDrivers {

// These firmware-side actions are deliberately separate from catalog/download
// transport and from hardware activation. The caller must serialize operations
// with other driver installs and obtain an explicit user decision for discard.
// Existing /Drivers/<id>/driver.elf + manifest.json packages remain compatible.
// A true return means inspection ran, not that the stage is valid; inspect the
// OrdinaryStageReview::state for actionable diagnostics.
bool inspectDriverStage(const char* id, RuntimePackages::OrdinaryStageReview& review);
RuntimePackages::OrdinaryTransactionResult retryDriverStage(const char* id);
RuntimePackages::OrdinaryStageDiscardResult discardDriverStage(const char* id);

}  // namespace RuntimeDrivers
