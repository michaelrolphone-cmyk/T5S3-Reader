# Focused architecture research queue

The next pass should answer the first unresolved question that can change a recommendation. Read source and history first; recommendations here do not authorize implementation or hardware experiments.

## 1 Display completion and quiescence

- Does the latest PR #220 still report completion on scan admission and certify quiescence after a void stop?
- Can existing backend completion signals unambiguously represent a submitted frame through all drive pulses and format changes, or is a provider-owned sequence/fence needed?
- What is the smallest compatibility-preserving propagation of actual teardown success?
- For the first actual LCD/AMOLED provider, which formats, geometry/stride, safe layout, refresh intents, completion semantics and inputs are supported? Which current client assumptions prevent adaptation, and which are legitimate declared app requirements?
- Can existing SDK fields express those requirements without new ABI? Do not invent panel behavior or require touch merely because the product is a watch.
- **Evidence needed:** exact client/provider/backend call chain and one discriminating executable test result from an authorized implementation, if available.
- **Decision:** narrow or retire P1 if those contracts are fixed; do not keep repeating a resolved defect. Native extraction remains a separate implementation decision.

## 2 Mixed-version capability and release witness

- Which original CrossPoint product revision and app deployment/build contract define the parity target? Establish the user-visible feature inventory before claiming parity; record which functions remain in the core versus move to apps/drivers without dropping UX.

- Verify old released CDC package with the U1 endpoint-requiring consumer: is rejection diagnosable and repairable without obscuring other providers?
- Verify new endpoint-capable package with the previously supported runtime; distinguish ABI safety from usable capability behavior.
- Which SDK revisions do independently produced artifacts actually build against? Do parity checks cover those SDK inputs, or only driver source/package metadata?
- **Evidence needed:** immutable runtime/package/SDK revisions, actual artifact identity, precise refusal/success behavior. Existing matching SDK blobs disprove current drift but do not guarantee future compatibility.
- **Decision:** prefer a small compatible-prefix/feature contract; propose new negotiation only if a real incompatibility requires it.

## 3 Acquisition peak memory

- Measure simultaneous retained candidate, temporary copy, relocated image and provider buffers in one display-plus-app and one chained-provider workflow.
- How much internal SRAM and contiguous PSRAM margin remains at activation and failed-quiesce quarantine? Which fallback allocations could consume control reserves?
- **Evidence needed:** per-class/owner peak measurements and largest-free-block values. Source arithmetic alone cannot answer fragmentation.
- **Decision:** promote P3 if meaningful pressure is observed; otherwise retain allocator behavior and demote budgeting implementation. Do not start storage VM by default.

## 4 Conditional package recovery check

Only promote a package proposal when concrete evidence defeats an existing invariant. Inspect whether interruption at each generation publication/recovery boundary exposes exactly one verified package generation, preserves unknown user data, and prevents replacement while physical quiescence is uncertain. Distinguish process interruption from actual SD power-loss durability; do not assume directory rename supplies a hardware durability guarantee.

**Evidence needed:** latest transaction/use-gate source, targeted fault-injection coverage and actual filesystem semantics. Current transaction and pinning foundations are reasons against a rewrite.

## Review hygiene

Recheck Reader master and the two PR heads before interpreting new changes. Compare exact-head CI to source-derived claims; green CI is not physical qualification and stale prose is not a failing check. Reconcile migration repo heads when compatibility evidence changes. Do not edit code, configuration, builds, manifests or versions, and do not merge or release from this document-maintenance work.
