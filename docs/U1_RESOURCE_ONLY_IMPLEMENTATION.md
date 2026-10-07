# U1 non-executing resource services

## Contract and minimum supported use

The bundle specification makes ELF conditional on the package's kind/runtime
contract. Ordinary manifest schema3 explicitly adds `payload`: `executable`
retains the executable contract; `resources` is a non-executing **service** with
`artifact:null`. Schema1/2 keep their existing mandatory ELF rules. No absent
ELF fallback, fake capability, fifth kind or second installer is introduced.

The minimum use case is an independently versioned help/dictionary/data pack
under `/Services/<id>/`, consumed read-only by an already authorized application
or installed driver/service/provider. Data-only packs have declared bounded
nonempty leaves, no ELF entries, no capability requirements and no resource
imports. They are installed, upgraded, inventoried and removed by the ordinary
engine; capability discovery and provider graph admission explicitly skip them.
They never start a task or publish a hardware capability.

An executable schema3 package can request at most four ordered
`resource_imports`, each with an ID and minimum numeric version. These are
optional read requests, not capability dependencies or automatic installation.
The existing manager policy permits only a declared import index, a matching
installed non-executing service and a declared non-executable leaf. It does not
grant arbitrary package paths, write access, or hardware rights. A missing pack
fails that request without preventing unrelated application execution.

`open_import` is an optional tail on the existing app resource table and the
provider resources host table. Callers must check `struct_size` before using the
new member. Existing prefixes and scoped own-package access remain intact.
The caller's authenticated invocation/private provider context and admitted
package identity are checked before and after opening. The existing stream
registry carries the read-only handle; target package-use pins block managed
replacement until safe close. Provider contexts retain their four-handle quota.
Observed quiescent storage generations are required during import admission and
checked before/after reads and seeks. Raw compatible storage uncertainty refuses
imports until safe reconciliation; this is not a persistent integrity receipt.

## Delivery and verification

`Services/<source>/manifest.json` can declare exactly type, id, version, payload,
architecture, min_runtime_api and resources. The existing manifest discovery,
selective build, ordinary packer, isolated export, immutable record/index and
runtime URL path carry the data-only payload without invoking an ELF builder.
Source and installed inventories remain exact; transport and staged readback
retain SHA-256. Source resource bounds are 16 files, 1 MiB each, 4 MiB total and
4096-byte ordinary manifest, within the existing runtime package limits.
Executable source manifests with resource requests emit schema3; unchanged
sources retain their prior schema. Older readers safely reject schema3 rather
than misinterpreting missing code as a valid executable package.

Observed local tests include actual source-to-ZIP-to-record/index/runtime URL,
ordinary staging SHA/readback and directory revalidation, upgrade rename faults,
retry, active-use refusal and interrupted uninstall recovery. App bridge tests
consume actual bytes and reject undeclared/traversal/stale-version/executable
substitution, mutation uncertainty and revoked contexts. A real host-loaded
provider ELF uses the new host-table tail, reads the same data-only pack through
its private context, rejects other requests, and loses access on quarantine.
These reuse the production parser/installer/bridges, with filesystem fakes and
host-loaded ELF respectively; they are not physical SD execution.

Review also found that ZIP staging, shared by offline and online installation,
still retained a flat-only implementation through bd9b09a7 despite earlier
directory-intake coverage. It now reuses the existing declared-tree primitives.
The actual extracted production Stage fails nested open at bd9b09a7 and passes
with this change, including failed parent creation, unknown data and iterator
fault preservation. Preliminary caller/target metadata reads now hold their
own use pins and retain them on uncertain close; all three metadata-close
positions have explicit regression coverage.

Both firmware/host workflow36835619954 and provider workflow36835620195 passed
published c80bdee1, identical tree8fd2c598 to locally tested b5cba2b2. No shipped consumer
or new pack was added merely as a witness, and no live catalog/release was
changed. Font/theme activation, arbitrary cross-package writes, transitive
resource dependencies, durable receipts, cryptographic authenticity and physical
qualification are not provided by this minimum data-only contract. Durable
admission verification and lower-I/O termination remain separate U1 gaps.

**Implementation In Progress**
