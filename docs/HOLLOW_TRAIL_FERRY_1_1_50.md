# Chapter V: taking the ferry line

The existing first ferry now starts at its existing far-bank stop, x632. At the
reachable near-mooring strip x322–343, Confirm examines the same live boat.
Confirm raises a hand and calls; the unanswered wait stops after 96 fixed steps.
A second deliberate Confirm takes the line, then holding Left pulls the actual
boat toward its original near stop x398. The original hull, grotto, water raster,
channel bounds, boarding contacts, rowing physics and far-bank landing remain.
The bent canvas and pale oar remain ambiguous at a distance; there is no confirmed
occupant, named person, new evidence, puzzle input or ending verdict.

The pull takes at most 312 moving steps, with one constant-time update per step.
Taking the line has a bounded 32-step reach. Both hands and the drawn line use the
same world-space grip. Releasing Left holds the boat where it is. Pause, journal,
controller faults and source rearm freeze progress; Cancel leaves the real boat
at its partially retrieved location. Re-entering resumes from that location.
Completion holds statically until Confirm returns to the same bank. The normal
boat controller then owns boarding, rowing and landing. Cover/oar/reed details
remain visible in the ordinary world during the handoff, so the boat does not
change appearance when examination closes. No second boat is created.

Death before completing retrieval preserves partial progress. Once retrieval is
complete, retry at the near checkpoint returns the boat to its original near
stop; retry beyond the crossing retains the far-bank stop. Chapter restart
restores the initial far-bank composition. The input-only ten-chapter route now
performs the deliberate rope interaction before its existing boat actions.

## Validation

- New ferry contact/controller test: HID and XInput; static ambiguity/unanswered
  holds; 312-step bound; pause, journal, fault/recovery and neutral rearm;
  partial cancellation/re-entry; unchanged player, evidence and puzzle; original
  boarding/rowing/landing; checkpoint retry; reachable arms and legs; deterministic
  gray rendering. AddressSanitizer and UBSan pass with leak detection disabled
  because LeakSanitizer cannot run under this executor's ptrace environment.
- Original grotto test: all 300 independent reference rasters and 10 complete
  nearest-camera frames are unchanged.
- Production renderer, full route/final-door, controls, ground contact (25,296
  poses), character/oar contact and waiting-awning tests pass. The old renderer
  and reflection fixtures explicitly place their boat at the original captured
  near stop; their golden pixels were not changed.
- ESP32-S3 app build, structural ELF validation, manifest/catalog/package digest
  checks and offline bundle verification use the supplied Xtensa compiler.
- App identity stays within cumulative unreleased 1.1.50 (master lineage 1.1.49);
  minimum firmware remains 1.3.37. No remote write, release or device test.

Initial source-labelled before/after gray and production packed-mono captures
were inspected locally before the owner asked to skip further screenshots.
Those earlier snapshot captures are preserved in the bounded evidence archive;
they are not represented as final-source images. No screenshot upload was tried.

## Published progress checkpoint

This implementation is included in the continuing PR436 source progress.
Source, focused tests and package are preserved in the delivered checkpoint
archive. New screenshot deliverables are temporarily deferred at the owner’s
request; existing captured evidence remains intact. The PR records the exact
status of previously published and held media.
Aggregate results and current CI are reported separately, without claiming
whole-book completion, a merge, release or device validation.
