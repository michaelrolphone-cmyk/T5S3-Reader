# Recommended guidance for changes under `Apps/`

Read the repository root [`AGENTS.md`](../AGENTS.md), [RiscRTE platform specification](../docs/RISCRTE_PLATFORM_SPEC.md), and [Application Version Policy](../docs/APP_VERSION_POLICY.md) as the normal context for app work.

This file records preferred app-development practice. Use it as the default when the current task does not specify a different approach.

## Versioning recommendations

For a distributable app modification, the normal release practice is to give that app its own version increment. Before modifying `Apps/<name>.c`, distributed resources, app-specific build options, or installed metadata, preferably inspect the sibling `Apps/<name>.json`, the PR merge base/target branch, and the most recently distributed version.

The preferred convention is to advance the manifest `version` to a strictly higher `MAJOR.MINOR.PATCH` value in the same change set. One increment per cumulative unreleased update is normally sufficient; a later independently published update normally gets another increment. When several apps change, each affected distributable is normally versioned independently.

A firmware version change, `min_firmware_version`, artifact SHA-256, release tag, or catalog regeneration is not normally treated as the app's own version. Prefer changing `min_firmware_version` only when the app genuinely depends on newer firmware API or behavior.

Before treating ordinary app work as complete, the preferred evidence is:
- record `app: old_version -> new_version` for each affected app;
- run `scripts/build_all_apps.py` or the relevant app-specific builder where available;
- check ELF, sidecar, catalog, and manifest version agreement;
- avoid intentionally republishing changed bytes under an unchanged app identity/version unless the current task specifically calls for that experiment.

If CI is unfinished, report it as pending rather than as passed and continue other useful work. Docs-only changes and changes that do not alter a distributable app normally do not need unrelated app version increments.

The changed-app version guard is intended as automated enforcement of this default practice. If it is not implemented or cannot be verified, a manual comparison is normally sufficient for development work. A current task can intentionally use different versioning or publication handling for a test, temporary increment, experiment, recovery, or other task-local reason.

## Long-running operation recommendations

For app file browsing, downloads, hashing, ZIP/resource processing, installation, and other potentially long loops, use [Bounded, Cooperative Long-Running Operations](../docs/COOPERATIVE_BOUNDED_OPERATIONS.md) as the preferred design reference.

Good default state-machine behavior includes bounded memory/work/retries/deadlines, avoiding unnecessary repeated full scans, checkpointing by both work and elapsed time, genuine scheduler cooperation rather than watchdog reset alone, throttled progress reporting, and recovery behavior that preserves prior data where practical.

When those mechanisms are absent from production code, normally treat that as implementation work to improve rather than as a reason to wait for hardware testing or build an exhaustive milestone-specific qualification suite.
