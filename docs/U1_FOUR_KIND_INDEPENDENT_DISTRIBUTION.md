# U1 independent delivery for all four package kinds

The existing independent release pipeline now covers applications, drivers,
services and providers end to end. This changes code and offline verification;
no live release, index update or workflow dispatch is part of this checkpoint.

## One existing path

Source manifests feed the existing ordinary provider source discovery and
conventional builders. Selective planning/building remains version-based; it
does not rebuild or republish an unchanged dependency merely because another
package requires it. The common packer/exporter emits the existing ordinary
`.rte.zip` contract. Record verification checks exact source kind, version,
artifact, dependencies, ABI profile, inventory, sizes and hashes against the
canonical ZIP and its isolated product catalog.

Service/provider products use their own immutable tags and isolated
`release-service-packages` / `release-provider-packages` artifact directories.
The existing driver and app directories are retained. Export selection checks
kind as well as ID: a requested ID staged with another kind fails. Source builds
retain the current global module-ID uniqueness rule and reject app/module ID
collisions before invoking builders because linked/staging directories remain
flat. Runtime/catalog identity is still `(kind, id)`; this conservative source
build restriction prevents reuse of a different package's same-name ELF.

The same release workflow plans, builds, verifies, uploads/restores and
revalidates each requested product before its existing publication step. Every
product's success is required if requested. Source/CI triggers include generic
service/provider/driver manifests and conventional builders, rather than a new
class allowlist. Normal app/GameBoy/firmware index updates preserve any existing
service/provider arrays. CDC alias retirement remains driver-specific.

## Bounded runtime contract and compatibility

Independent index schema1 retains its required historical fields. The new
services/providers arrays are optional when absent; when present, they require
ordinary ZIP records with their exact kind/tag/asset/URL. New kinds never use
legacy loose-ELF fallback. Numeric version barriers, immutable equal-version
conflict rejection, source architecture filtering, cancellation and clearing
partial output are preserved. Aggregate catalogs remain unchanged.

Each new array has64 rows; the existing128 apps and64 drivers remain. Serialized
input stays capped at512KiB, final merged selections at64 packages, and the
independent structure has a compile-time300KiB ceiling (240,264 bytes in the
host build). Bulk catalog objects remain in PSRAM with checked allocation.

Current master1e0188c1's App Store uses ArduinoJson and ignores additional root
arrays. Earlier unmerged U1 snapshots used a stricter parser; no deployed-reader
breakage is inferred from those branches. Existing documentation still states
that old firmware cannot consume the U1 app ZIP record format. Historical loose
app/driver input remains supported by new firmware.

## Evidence

Tests cover four-kind packer→record→index→actual C++ immutable URL round trips,
new-kind corruption and wrong-source ABI, same-ID cross-kind runtime selection,
version conflicts, overflow, optional-array reset, unchanged-row preservation,
selective builders, source collisions before build, isolated restoration and
workflow failure gating. Generic provider fixtures do not invent a new shipped
provider: the repository currently has the actual archive service and no
Providers source directory.

The independently downloaded, SHA-verified014fb6f7 archive service ZIP also
round-trips through the new record/index/runtime path. Its archive digest is
`2d0699edbd825901e9ec71380a9d5bc32147e9882b02775b805d1efbfaa9b565`.
The existing provider CI now repeats this with its own newly built actual
service/provider artifacts and preserves a compact offline record report.

The next necessary firmware build also emits a compact source-hashed target
reference report: selected serial/USB entrypoint disassembly, direct calls,
indirect-call observations and literal/API-object function-pointer references.
This is evidence for review, not an assertion that every indirect call or
physical PHY path is proven. It does not use the earlier inaccessible firmware
artifact. Source head, compiled checkout and ELF hash are recorded separately.

Target checks for this distribution checkpoint remain pending. Durable
receipts, resource-only semantics, lower blocked-I/O termination, final
reachability/PHY evidence and final firmware-version integration remain open.

**Implementation In Progress**
