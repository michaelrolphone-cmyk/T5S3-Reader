# Bug repair engineering reference

## Bug records

`bugs.md` contains canonical bug entries.

`docs/BUG_FIX_PROGRESS.md` contains repair progress, branch and PR references, test evidence, and status notes.

A useful bug record contains:

- stable bug ID;
- affected repository and revision;
- affected files and functions;
- observed failure;
- reproduction or trigger conditions;
- likely cause;
- impact;
- repair direction;
- repair branch and PR references;
- validation results.

## Deduplication data

New findings can be compared by repository, source revision, affected function, trigger, failure mode, and repair.

Duplicate reports retain their source provenance while linking to the canonical bug entry.

## Repair implementation

Repair work records the source revision and changed files.

A focused regression demonstrates the original failure and repaired behavior where practical.

Validation results distinguish:

- focused tests;
- aggregate tests;
- target builds;
- CI;
- host simulation;
- synthetic fixtures;
- hardware results.

## Version data

Changed distributable components use the project’s app, driver, service, provider, or firmware version metadata.

Version changes are recorded with the repair when applicable.

## Repair states

Common repair-state labels include:

- Needs revalidation
- Confirmed
- In progress
- Awaiting merge
- Fixed on master
- Duplicate
- Not reproduced
- Obsolete
- Blocked

Each state record includes the checked revision and available evidence.
