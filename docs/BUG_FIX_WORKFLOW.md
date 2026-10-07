# Recommended scheduled bug repair workflow

This document records the preferred coordination and evidence model for the existing bug scan/repair jobs. It is advisory repository guidance, not an authority boundary. Direct user instructions for a particular run or repair may intentionally supersede, bypass, consolidate, or vary this workflow.

Use root `AGENTS.md` and the current platform scope as the normal context.

## Preferred canonical inventory and single-writer model

- `bugs.md` normally owns stable canonical report IDs. `docs/BUG_FIX_PROGRESS.md` normally records the active run, claims, branch/PR identity, verification, blockers, and next action.
- `automation/bug-ledger` is the preferred coordination branch. At run start, normally refresh master and this branch and read both ledger files from the latest remote head. A scan branch's local numbers are best treated as aliases.
- Prefer one coordinator writing the shared ledger at a time. Workers can return evidence and proposed report text while keeping source edits in their own worktree/repair branches. This is a collision-avoidance convention, not a permission boundary.
- Before ordinary implementation, the preferred process is to publish a run/bug claim containing owner, baseline SHA, canonical IDs, branch, timestamps, and phase. Re-read the remote head before writing. Prefer a non-force fast-forward update from the captured ledger head. If another writer advanced the branch, normally reread and reconcile rather than overwrite.
- Later invocations should normally avoid duplicate writers or duplicate repairs. If a run appears active, prefer continuing or coordinating with that run. A stale timestamp alone is weak evidence that a claim is abandoned; inspect current branches/PRs/work first. A direct user instruction may intentionally reassign, consolidate, or supersede ownership.
- When publishing ledger changes to master under the normal workflow, prefer the same coordination PR rather than overlapping ledger PRs. Preserve newer canonical entries and claims during backmerge and normally keep existing canonical IDs stable.

## Preferred scope and batch size

- A useful default is at most two outstanding bugfix code batches. The ledger-only coordination PR does not normally count as a code batch. A batch is typically 2–4 related small fixes or one substantial fix.
- Normally inspect open PRs #324, #277, #220, #194, and #96 for path overlap before selecting work, and avoid changing their branches unless the user's task specifically calls for doing so.
- The default automation flow keeps ledger changes on the coordination branch and source changes on focused repair branches rather than writing directly to master, merging, releasing, deploying, or editing unrelated repositories. These are workflow defaults; a direct user instruction may specify a different route.
- Historical candidate batch: #16 and #17 (Button Remap persistence rollback and misleading Reset/Cancel labels), subject to current revalidation.

## Preferred single-run sequence

1. **Resume useful existing work first.** Inspect the exact repair PR/head and pending checks. Fix attributable failures or record the real blocker. A closed-unmerged PR is normally treated as unfinished rather than as a merged fix.
2. **Reconcile scan evidence.** Compare immutable source commit plus scan-local ID, affected function, trigger, failure, and repair. Attach duplicate provenance to an existing canonical report where appropriate; assign a fresh ID for a distinct failure.
3. **Select and revalidate.** Check current master plus merged/open/closed-unmerged PRs for overlap and record the checked SHA, source locations, and actual reproduction/test result.
4. **Record ownership when useful.** Prefer a durable claim before source changes. Reuse an existing repair branch when resuming unfinished work; otherwise branch from current master. Implementation PRs normally target master rather than another repair PR.
5. **Implement a focused repair.** Prefer a regression that fails on the original behavior. Follow the repository's normal package-version guidance for changed distributables unless the user's task directs otherwise. Exercise normal, error, retry, and cleanup paths relevant to the defect.
6. **Run proportionate checks.** Distinguish passed, failed, pending, and unavailable tests. Keep missing toolchains and environment-limited sanitizers visible. Avoid describing a focused pass as a full build or hardware qualification.
7. **Publish the repair.** The normal workflow uses one draft repair PR, recording its URL, exact remote SHA, version changes, test results, limitations, and next step. Continue recoverable work while useful; leave a resumable record if blocked. A direct user instruction may request a ready PR, direct branch publication, consolidation, or another route.
8. **Track state accurately.** Use `Awaiting merge` for an unmerged PR and `Fixed on master` after verifying merge ancestry/source and the applicable regression. Merge, release, deployment, and flash are ordinarily separate activities from a repair PR, but may be combined when the user's task explicitly asks for them.

## Recommended evidence states

- **Needs revalidation:** preserved source report; no fresh current-master assertion.
- **Confirmed / claimed / in progress:** exact baseline, owner, reproduction, and repair branch recorded.
- **Awaiting merge:** fix has an unmerged PR; pending/failed/unavailable checks recorded separately.
- **Fixed on master:** merged commit and current verification recorded.
- **Duplicate:** source alias retained and linked to canonical ID.
- **Not reproduced / obsolete / blocked:** reason and checked commit recorded without mislabeling as fixed.

The initial restoration record is in [BUG_FIX_PROGRESS.md](BUG_FIX_PROGRESS.md). Scheduled jobs normally read it before selecting work. Scan reports may mention obsolete signed-package paths; prefer inspecting actual reachability and current signing-purge direction rather than reviving historical behavior accidentally.

Nothing in this document determines whether the user has authorized work to proceed. It describes the normal coordination method only.
