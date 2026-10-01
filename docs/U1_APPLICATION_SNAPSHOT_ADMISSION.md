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

This application slice is currently local and target verification is pending.
The preceding receipt/provider frame correction **c33cb044** passed both exact-
head workflows **36840317554 / 36840317634**, retaining the 384-byte recursive
registration guard. The earlier independent receipt review did not complete;
no later review result is inferred from that failure.

Remaining implementation/evidence is explicit:

- Loose legacy app paths keep their existing compatibility/temporary installed-
  load contract; this new canonical invocation context does not silently rename
  or upgrade them
- Retained provider snapshots with an older source-read epoch remain conservative
  on remapping, as documented in U1_VERIFICATION_RECEIPT_IMPLEMENTATION.md
- Observed storage coherence is not authentication or a universal detector of
  unobserved media edits. Cold admission covers executable and used metadata,
  while install/recovery/explicit verification still check every declared leaf
- Target callback binding, remaining lower-I/O termination, complete indirect/
  PHY evidence, final master/version reconciliation and owner qualification stay
  separate from these local host results

**Implementation In Progress**
