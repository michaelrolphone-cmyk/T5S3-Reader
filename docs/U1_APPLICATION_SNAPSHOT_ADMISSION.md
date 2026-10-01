# U1 canonical application snapshot admission

## Connected path

Canonical `/Apps/<id>/<artifact>` launches now capture one private invocation-
owned admission context after the existing app/package-use scope is established
and before `launch_elf_app`. The exact ordinary manifest is parsed against current
runtime policy and bound to the host-selected ID/version/artifact. Its declared
sidecar is read once, SHA checked and retained immutably under the invocation ID.

Existing mandatory dependency checks and optional provider declarations consume
that same captured sidecar through their existing parsers and authorization.
They do not reopen a mutable file to acquire a different allowlist. Parsing,
compatible firmware, file-name/version checks, capability selection and runtime
grants remain mandatory. A digest or receipt does not grant a hardware right.
The context adds no provider registry, installer, alternate path resolver or SDK
entrypoint.

The existing `esp_elf_open` reader now closes its source descriptor successfully
before handing its already-owned buffer to a private managed-app callback.
That callback reuses the receipt/provider admission helper: cold/uncertain
boundaries check actual buffer bytes; unchanged quiescent proof can avoid another
ELF hash. The source stamp was captured before the original metadata and ELF
reads, never sampled afterward to promote old bytes. Before capture, safe
reconciliation may refresh SdFat after a completed raw-FS session; refused
remounts retain cold behavior instead of fabricating quiescence. Relocation consumes this
same admitted buffer, without a pathname reopen. The callback rechecks the live
invocation after I/O so late work cannot survive revoke.

Standalone loader ports without the core callback fail closed for canonical
application paths; their existing loose-file contract remains available. The
callback is absent from public ELF import registration. The normal target build
requests compact references for the linked loader/callback/helper and symbol
binding, alongside the existing serial evidence, rather than assuming a strong
callback won solely from source presence.

## Ownership and failure behavior

The context holds its own package-use pin. Copied firmware metadata views keep
that pin alive until their final reference ends, even after the app context is
retired. New requests require the same running invocation and exact path.
Metadata close uncertainty retains the context's pin instead of allowing
replacement. A pre-mapping admission rejection releases only the host's own
launch pin; uncertain metadata retains its independent ownership. Existing
failed-dlclose mapping pins remain unchanged.

The loader frees the owned buffer on read, format, source-close or admission
failure. A failed source close never proceeds to guest relocation and is not
blindly retried on a possibly reused descriptor. The existing SD VFS remains
read-only and bounds ELF files to 8 MiB. This does not terminate a lower SD call
that fails to return.

## Evidence and limits

The real HalStorage/context helper fixture covers exact manifest/sidecar bytes,
private path/owner/use-pin boundaries, changed sidecar versus retained immutable
metadata, changed ELF refusal, revoke and uncertain-close retention. The actual
`esp_elf_open` function and weak fallback are compiled from production source;
fixtures verify strong callback dispatch after close, the copied buffer rather
than changed source bytes, all failure cleanup paths, and canonical denial when
only the standalone fallback is linked. Existing app/USB launch-order guards and
native-app aggregate remain connected. These are host fixtures, not physical
execution of native machine code.

This application slice is published and target-verified at
**ad7a0431af1c18e95cce96dbf4337cf79b01e7df**, matching locally tested **a59642d5**
and tree **07c794fe3441a3760dd746a15e077f7a17677ca7**. Both workflows
**36842590302 / 36842590322** succeeded. The compiled PR merge checkout was
**d66340a64d72dc3a86b2303c60684352dc810f31**.

The downloaded compact target reports were ZIP-SHA checked:
- T5 artifact11152406596: e18579b122d7da087b30fdb5a8bdafc2d818509fde5d7458b1a594256e524def;
  firmware ELF c1b95457b9a98dd3a706a28d613bc05ea34c2cb17a65ede10ad8587e8b777751
- EPD artifact11152805198: 96c180e5642b999a657299914f03ab4e177d38f3a6d3de1089f94db023d0eaed;
  firmware ELF4a9e6b0bbb95cb1f320487bc718542dd9618836035e4bf429530cee49bfe5107

Both link the strong global callback, rather than the standalone weak fallback.
`esp_elf_open` loads its callback address from a literal and executes callx8;
the callback directly reaches `admitInstalledExecutableSnapshot`. The buffer
register passed to admission is subsequently stored as the returned payload.
This resolves that selected call edge, not every indirect call in the firmware.
The preceding receipt/provider frame correction **c33cb044** passed both exact-
head workflows **36840317554 / 36840317634**, retaining the 384-byte recursive
registration guard. The earlier independent receipt review did not complete;
no later review result is inferred from that failure.

## Checksum-bearing loose input

The same private invocation context now supports loose ELF paths. It captures a
bounded sidecar once and uses the existing real AppManifest parser for paired
size/SHA, compatibility and filename checks. Mandatory and optional capability
parsers consume that captured sidecar; missing legacy version remains accepted.
The loader-owned buffer is checked against any declared size/SHA before mapping.
Warm reuse shares the existing 32-slot RAM memo and requires the original
quiescent storage epoch, exact path, sidecar digest, executable digest and size.
Long paths remain cold-verified rather than truncated into a cache key.

No canonical identity/version or durable loose receipt is fabricated. A valid
digestless manual sidecar, or missing sidecar without observed storage error,
keeps the existing manual-input contract. Format/import checks still apply;
there is no claimed checksum authority for undeclared bytes. Raw uncertainty
stays cold. A revoked matching invocation cannot silently use the standalone
loose fallback. Existing package pin/mapping failure rules remain unchanged.

The actual AppManifest parser tests and real-parser + HalStorage + invocation
fixture pass against ArduinoJson7.4.2, as does the full native-app host aggregate.
Fixtures cover actual byte SHA, warm reuse, mutation and size refusal, revoke,
digestless/missing sidecars and an observed metadata error during absence.
Both normal board CI jobs now run the real parser fixture using their already
installed dependency. Target verification of this loose extension is pending.

Remaining implementation/evidence is explicit:

- Retained provider remapping passed both workflows at a928d444
  (36845426266 / 36845426285)
- Observed storage coherence is not authentication or a universal detector of
  unobserved media edits. Cold admission covers executable and used metadata,
  while install/recovery/explicit verification still check every declared leaf
- Real lower-I/O termination, complete indirect/PHY evidence, final master/version
  reconciliation and owner qualification remain open
- The blocked independent receipt review remains incomplete; these tests do not
  replace or imply a completed review

**Implementation In Progress**
