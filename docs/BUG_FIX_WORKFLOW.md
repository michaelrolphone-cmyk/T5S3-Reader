# Scheduled bug repair workflow

This document defines coordination and evidence rules for the existing bug scan/repair jobs. It does not merge code, create a new release gate, or authorize a release or hardware flash. Follow root `AGENTS.md` and the current platform scope for each repair.

## Canonical inventory and single writer

- `bugs.md` owns stable canonical report IDs. `docs/BUG_FIX_PROGRESS.md` owns the active run, claims, branch/PR identity, verification, blockers, and next action.
- `automation/bug-ledger` is the coordination branch. At run start, refresh master and this branch, then read both ledger files from its latest remote head. A scan branch's local numbers are aliases only.
- Exactly one coordinator writes these ledger files. Workers return evidence and proposed report text; they do not edit the shared ledger independently. Keep each worker's source edits in its own worktree/repair branch.
- Before implementation, the coordinator publishes the run/bug claim with owner, baseline SHA, canonical IDs, branch, timestamps, and current phase. Re-read the remote head immediately before writing. Create the proposed commit with the captured remote ledger head as its parent and use a non-force fast-forward ref update. Never force-update the coordination ref. If another writer advances the branch, the losing divergent update must be rejected; stop, reread the winning head and claims, and reconcile rather than overwrite.
- A later invocation must not start a second writer or duplicate a claimed repair. If a run is active, continue that run only through its recorded owner or skip new work. A stale timestamp alone is not permission to steal a claim: establish that its owner has stopped, inspect remote branches/PRs and uncommitted work, record recovery, and then take over. Alternatively, an explicit current owner instruction may supersede the recorded coordinator: record the instruction, prior owner and any unknown lifecycle/local-work state without inventing a stop; preserve all remote history and publish from the captured current parent using a non-force update. The superseded writer must reread current ownership before any later publication; any divergent stale write must fail. If neither safe recovery nor explicit coordinated supersession is available, record/report the blocker.
- When publishing ledger changes to master, use the same coordination PR rather than creating overlapping ledger PRs. Preserve newer canonical entries and claims during backmerge. Never renumber existing canonical IDs.

## Scope and batch limits

- There is no fixed numerical cap on outstanding bugfix code batches. Sequence work by user priority, active ownership, overlap, and evidence. The ledger-only coordination PR is not a bugfix code batch. Each batch should remain coherent: 2–4 closely related small fixes or one substantial fix; the recovered single #205 repair was the initial exception.
- Avoid overlapping open PRs #324, #277, #220, #194, and #96; recheck their state and changed paths before selection. Do not mutate their branches.
- No direct master writes, merges, auto-merge, release, deployment, or cross-repository edits. Keep ledger changes on the coordination branch and source changes on focused repair branches.
- Select the next candidate only after checking the latest canonical inventory, current-master reachability, active owner claims, existing repair branches, and all open or closed-unmerged PR paths. The former #16/#17 Button Remap batch was completed through merged PR #333 and must not be requeued.

## One run

1. Resume recorded work first. Inspect the exact repair PR/head and pending checks; repair authorized failures or record a specific blocker. A closed-unmerged PR is unfinished work, not a merged fix.
2. Reconcile new scan evidence by immutable source commit plus scan-local ID. Compare the affected function, trigger, failure, and repair. Attach duplicate provenance to the existing canonical report; assign a fresh ID only to a distinct failure. The latest reconciled inventory currently ends at canonical ID 260; assign a new canonical ID only after checking the current ledger head.
3. Select a ready unclaimed report. Revalidate its trigger on current master and check merged/open/closed-unmerged PRs for overlap. Record the checked SHA, source locations, and actual reproduction/test outcome. Historical `Open` is not current confirmation.
4. Claim the report durably before source changes. Reuse an existing authorized repair branch when recovering its unfinished work; otherwise branch from current master. Implementation PRs target master, never another repair PR.
5. Implement a focused fix and regression that fails on the original behavior. Honor package version rules for every changed distributable. Check normal, error, retry, and cleanup paths relevant to the defect.
6. Run focused tests, applicable syntax/build/aggregate checks, and exact-head CI. Distinguish passed, failed, pending, and unavailable checks. Missing toolchains and environment-limited sanitizers must remain visible. Do not call a focused pass a full build or hardware qualification.
7. Publish or update one draft repair PR when authorized. Record its URL, exact verified remote SHA, version changes, test results, limitations, and next step. Continue owned checks/recoverable failures while the run is active; leave a resumable record if blocked.
8. Mark `Awaiting merge` when the change is in an unmerged PR. Mark `Fixed on master` only after verifying merge ancestry/source and the applicable regression. Merging, release, deployment, and flashing still require their own authorization. Clear the active run only after its owned work is terminal or has a specific resumable blocker; retain the PR/integration record.

## Evidence states

- **Needs revalidation:** preserved source report; no fresh current-master assertion
- **Confirmed / claimed / in progress:** exact baseline, owner, reproduction, and repair branch recorded
- **Awaiting merge:** fix has an unmerged PR; record pending/failed/unavailable checks separately
- **Fixed on master:** merged commit and current verification recorded
- **Duplicate:** retain source alias and point to canonical ID
- **Not reproduced / obsolete / blocked:** record why and the checked commit; do not mislabel these as fixed

The initial restoration record is in [BUG_FIX_PROGRESS.md](BUG_FIX_PROGRESS.md). Scheduled jobs must read it before selecting new work. Scan reports may mention obsolete signed-package paths; inspect actual reachability and obey current signing-purge scope rather than revive signing.
