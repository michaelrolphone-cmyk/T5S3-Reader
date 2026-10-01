# U1 archive.zip service

## Connected service and bootstrap boundary

`Services/archive_zip` defines the new **service/archive-zip 0.1.0** package,
using the existing installed provider graph and `archive.zip@1` capability. Its
only dependency is the installed generic `platform.clock@1` provider. It has no
hardware imports, firmware ZIP proxy, path-based access or private installer.
The historical provider descriptor/executable names remain compatibility ABI
names; the ordinary manifest/catalog kind is `service`, installed under
`/Services/archive-zip/`.

`RiscArchiveZipV1.h` exposes begin/append/seal/count/entry/read/close. A caller
keeps its independently authorized capability lease, reads its scoped input
stream, supplies copied chunks and receives extracted bytes into its own buffers
or scoped output stream. The service never receives a filesystem path or keeps
a caller buffer pointer. Job tokens are bearer handles bound to the loaded
provider generation, not filesystem authority. Capability interfaces remain
borrowed under the existing owner-task/lease contract.

The same `PackageRteZip.h` structural decoder, contiguous topology and CRC
validator serve ordinary installation and this service. The shared primitive
can explicitly inspect a non-package ZIP; the installer keeps its default
mandatory `.package.json` path and unchanged SHA/preflight/transaction checks.
No installer call depends on `archive.zip`, so missing service/network cannot
prevent offline package bootstrap. The clock package can be installed first
through that same bootstrap, then the service package. Existing ordinary
four-kind install tests run without this service being loaded.

## Explicit v1 bounds and feature subset

- One job per loaded service; fixed128-KiB input storage,17 entries maximum
- At most512 copied bytes per append/read call; no retained pointers
- Stored ZIP only: no compression, encryption, data descriptors, comments or
  extra fields; unsupported features fail explicitly
- Canonical lowercase bounded relative names, implicit parent directories,
  no links/special files, traversal, aliases or file/directory prefix conflicts
- Empty ZIPs and zero-length regular files are supported
- Complete local/central topology and all entry CRCs validate before listing or
  extraction; append cannot mutate a sealed generation
- Monotonic60-second job deadline; byte/item and elapsed-time checkpoints call
  the clock's real scheduler-yield primitive; expiry permits explicit cleanup
  and bounded reclamation on the next begin
- Non-waiting provider-local OS mutex admission rejects concurrent/reentrant calls. Quiescence
  marks stopping before trying admission, denies subsequent work, and returns
  false while a call is active so the graph retains the ELF and dependencies
- Token generations do not wrap; close, capability release and successful
  quiescence clear owned input without deleting any file or user data

This bounded stored subset is the service's advertised contract, not a claim
that all third-party ZIPs or the existing legacy archive API now use it. The
legacy archive bridge/ZipFile path remains unchanged. This work does not claim
to repair its separately tracked source candidates or certify lower SD/network
call termination.

## Packaging and evidence

The existing manifest-discovery builder now scans `Drivers`, `Services` and
`Providers`, retains dependency ordering and emits the manifest's actual kind.
It reuses the same ordinary packer and generic catalog/exporter. The archive
builder checks a software-only exact import/relocation map; CI builds its actual
Xtensa ELF, validates loader compatibility, includes its single `.rte.zip` and
checks manifest/ZIP/catalog identity, version, CRC/SHA and dependencies.

Host checks load the actual service ELF and actual clock ELF through the generic
graph, exercise listing/extraction, large bounded input, empty files/ZIP,
unsupported/path/CRC/link refusal, copied buffers, stale tokens after reload,
expiry/reclamation and an interrupted call with failed-quiescence retry. The
ordinary package aggregate remains green without requiring the service.
Target/package CI is reported for its exact published head; no device, released
asset or live-catalog operation is implied by host success. Existing independent
product-selection CLI still focuses on firmware/apps/drivers; generic service
ZIP/export/catalog production is connected here, and final release integration
must reconcile that distinction rather than pretend a service was published.

**Implementation In Progress**
