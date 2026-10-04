# Copied SD package metadata and optional provider admission

Firmware 1.3.120 follows the delivered 1.3.105 candidate on PR350. Its new
diagnostics identify a concrete software failure before battery/RTC activation:
the device rejects `/Drivers/<id>/._privileged-imports.v1` as an unexpected
initial-tree member, then its complete capability scan reaches the 15-second
deadline. The same rejection appears for the platform clock, I2C and other X4
packages. Working boot-selected touch is compatible with this failure:
`BootstrapModuleStore` verifies the declared files without enumerating their
neighbors, whereas ordinary runtime inspection previously rejected all
undeclared neighbors.

## Narrow runtime treatment

Only `inspectInstalledOrdinarySdDirectory` opts into host-copy metadata
tolerance. It accepts a regular `._` companion only when its exact suffix names
a declared file, a declared directory prefix, `.package.json`, or permitted
manager receipt metadata. Each possible companion has one fixed bitmap slot.
Exact regular `.DS_Store` is allowed once in each already-declared directory.
These inert bytes are never opened, hashed, interpreted, used as a real member,
or supplied to a provider. All genuine members must still be present with the
same names, types, sizes, manifest rules and ELF headers. Provider import and
execution authorization remain independent.

Unknown files, unknown `._` suffixes, aliases, duplicate entries and copy-like
directories still fail. Both exact-tree passes remain. HAL continues to reject
malformed names and read/close errors. FAT/SdFat supplies validated file or
directory metadata, not a general-purpose symlink inspection API; ZIP intake
retains its separate symlink and path checks.

Source/stage verification, explicit integrity verification and deletion retain
strict inventory ownership. This change does not silently delete copy metadata
or grant it package ownership. Ownership-changing operations can still refuse
such extra files safely. The policy is shared by X4 and T5 runtime inspection;
it does not reuse the broader font-discovery hidden-file filter.

## Bounded complete discovery

The capability resolver, graph selection and public provider enumeration share
one root-entry budget: 64 ordinary entries, at most 64 regular `._<canonical-id>`
copy companions and one regular `.DS_Store`. Every raw item still traverses the
existing checked cursor and cooperative yield. Package companions cannot consume
the ordinary-entry cap, and overflow never becomes successful truncated EOF.
Provider selection still detects late competing providers rather than choosing
an arbitrary accepted prefix. The capability scan's 15-second deadline is
unchanged; no partial snapshot is exposed.

After a complete valid, quiescent scan, the existing generation-bound snapshot
is reusable. Mutation, remount, raw/writer windows, failed reads or uncertain
closes retain their existing invalidation behavior. No new persistent cache,
background scan or hardware fallback is introduced.

## Evidence and limits

- The exact delivered nine-package SD ZIP was recovered unchanged: 860,463 bytes,
  SHA256 `7576624c93178f22de35f969ddc252ac654a9e09f42f2eda8c9912b3fa5a1f95`.
  On the old production inspector, all nine packages resolve before copy
  companions are added; all nine fail initial-tree inspection afterward and
  battery/RTC capability versions become zero.
- The repaired actual SD/FatFs/HAL/inspector/resolver fixture resolves battery
  API1 and RTC API2 with 45 package companions and nine root companions. With
  Finder records too, the cold exact-nine scan performs 351 sector reads and
  takes 1,634 modeled milliseconds at an injected 3 ms per sector.
- A standalone 40-provider copied-card fixture performs 2,786 sector reads:
  8,636 modeled ms at 2 ms/sector and 11,546 at 3 ms/sector. Ten warm captures
  perform zero volume calls and zero sector I/O. Slower workloads still fail
  the finite deadline. These are sensitivity models, not device speed claims.
- Focused tests cover real members/imports missing beneath retained companions,
  unknown files, copy-like directories, declared size/ELF-header failures,
  unchanged full SHA-256 rejection, nested companions, duplicates, strict
  nonmutating purge, root limits, late ambiguity and failed EOF/close behavior.

No driver, SDK ABI, GPIO, I2C, RTC initialization or battery profile changes are
part of this repair. A later chip-level failure or an RTC with invalid/unset time
must still be diagnosed from the existing provider errors; accepting the package
is not proof of a valid clock or a healthy gauge sample. No physical success is
claimed from these software checks.
