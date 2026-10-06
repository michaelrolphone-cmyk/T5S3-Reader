# X4 physical page-button direction parity

Baseline: X4 working 36891e71ab651152c618d60d1d248abb4a7f44ff.
Branch: fix/x4-page-button-direction. Firmware 1.3.128 -> 1.3.136;
x4pro-buttons 0.1.4 -> 0.1.5. No other distributable package changes.

## Reproduced gap

T5's physical-page button path composes Side Button Layout and Flip UI via XOR.
X4's installed button provider publishes LEFT/RIGHT, while the reader mapped
those to fixed Previous/Next. Actual MappedInputManager methods, reader page
selection and T5 policy reproduce the mismatch in both press and release modes
whenever exactly one of layout swap or UI flip is set.

## Provider-scoped repair

The unchanged input.navigation v1 callback prefix gains an optional tagged
traits suffix. Its base struct size, API version, NAT1 tag and suffix version
are checked before reading the physical page-pair flag. Only x4pro-buttons
0.1.5 declares this trait. Older/prefix-only or invalid traits remain semantic.
No new driver imports, hardware operations, button mappings or polling changes
are introduced by the descriptor extension.

The current live selected provider supplies the trait. A board-wide assumption
is deliberately avoided: replacing X4's default physical provider with a
semantic controller must not reverse that controller's arrows. Disabled,
unusable or revoked providers expose no physical-page trait. One provider frame
replaces the previous frame; no controller aggregation or new source registry
is introduced.

A reader-only query applies the established XOR policy to declared physical
LEFT/RIGHT events. PAGE_BACK/PAGE_FORWARD and UP/DOWN remain semantic. Synthetic
page/front-button taps (including T5's already-transformed physical injections)
retain their direction. Ordinary app/menu queries, Home/Back/Confirm/Power,
touch coordinates and tilt behavior are unchanged. Shared EPUB, TXT and XTC
readers use the same query.

## Current X4-base refresh (2026-10-06)

The existing PR422 branch includes X4 source b91bc323 and retains original
repair head 6f49c69b as its first parent. The only textual conflict is the
firmware reservation: 1.3.136 / upstream 1.3.143 advances to 1.3.152. All original
input/SDK/driver changes and tests remain unchanged; x4pro-buttons remains 0.1.5.
Normal and ASan/UBSan production direction regressions pass on the combined
tree. Exact-head target CI is recorded on the PR. Hardware is unavailable and
unrun, not a prerequisite for software integration. PR350/master remain intact.

A software-only composition check with PR423 reproduced missing existing
HalGPIO members in this test's minimal facade. Adding BTN_PCA, wasAnyPressed
and wasAnyReleased makes the complete page-direction regression compile and
pass against either this isolated repair or both repairs together. The facade
keeps real event-mask semantics; no production or package bytes change.

## Compatibility and delivery

Firmware 1.3.136 needs the matching x4pro-buttons 0.1.5 SD package for this fix.
Older installed button packages still work with their previous semantic mapping;
new packages remain usable on old firmware through the original callback prefix.
Use the existing safe inactive/offline update path and next-boot activation.
No active provider is force-replaced and no automatic install or device action
is performed here. The live release-index and tags contained no published
x4pro-buttons version when checked; 0.1.4 is the current source/delivered lineage.

## Verification

The regression compiles the actual button driver, MappedInputManager methods and
reader direction query. It covers both buttons, press/release, all layout/flip
combinations, debounce, held/repeated events, neutral rearming, semantic and
synthetic input, T5 no-double-transform, ordinary navigation, Back/Confirm/Home
and tilt. The original source fails the same direction assertion.

The real NativeNavigationInput consumer additionally tests replacing a physical
provider with a prefix-only semantic provider, distinct frame bits, trait
revocation during failed cleanup and successful reacquisition. ABI suffix tests
include null, prefix-only, every truncated size, wrong tag/API/suffix version,
unknown flags and disabled/unusable/quarantined state. No failed cleanup token
is discarded.

Local Xtensa build and relative-target/export/import checks pass: the provider
ELF has zero imports. Source, linked provider metadata, ordinary package and ZIP
all identify x4pro-buttons 0.1.5. The local compiler is the available Espressif GCC 8.4 Xtensa toolchain; hosted builds verify the repository-pinned GCC 14.2 compiler and full board
bundles. Historical checked-in dist snapshots are not deployment inputs; the
normal builders regenerate and verify source-versioned package bytes. Host GPIO/RTOS/display boundaries are fixtures, not physical
qualification. ASan/UBSan remain enabled; local LeakSanitizer is disabled under
ptrace. Full aggregate and exact-head hosted results are recorded on the PR.
