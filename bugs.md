# Bug Log

## 2026-09-26 scan

### 3. Rom Manager title-detail Up/Down scrolling is reversed

- **Status:** Open PR #212 on branch `fix/rom-manager-detail-scroll-direction` targeting `master`; fix implemented and Rom Manager patch version bumped from `1.0.16` to `1.0.17`.

- **Affected code:** `Apps/rom_manager.c`, `show_vimm_detail(const char *name, const char *path)`.
- **Trigger / reproduction:** Browse Vimm Vault, open a Game Boy title whose detail text is long enough to scroll, then use the controls labelled **Up** and **Down**.
- **Observed / logically demonstrated failure:** The chrome labels `previous_label = \"Up\"` and `next_label = \"Down\"`, but `T5_UI_EVENT_PREVIOUS` increments `scroll` while `T5_UI_EVENT_NEXT` decrements it. With `scroll == 0`, Up advances deeper into the document and Down cannot move at all; after scrolling, Down moves toward the top.
- **Likely root cause:** The scroll-delta conditions were inverted when the title detail view was added.
- **Impact:** Physical/button navigation contradicts the UI labels and the rest of the application navigation model, making long title details awkward to read.
- **Repair direction:** On **Up/PREVIOUS**, decrement `scroll` when `scroll > 0`; on **Down/NEXT**, increment `scroll` when `scroll < result.max_scroll_lines`. Add a focused test that checks both boundary conditions and direction.

## Duplicate check performed

At scan time, `bugs.md` did not yet exist on `master`, the repository has GitHub Issues disabled/no issue records returned, and the current open PRs were reviewed for overlap. PR #198 concerns GT911 touch capture, PR #194 concerns global Home shortcuts, and PR #96 is the U1 implementation branch. Closed PR #183 introduced the Rom Manager title-detail view but does not document the reversed Up/Down behavior above.


## 2026-09-26 scan — 11:49 MDT

### 4. Font Family selections are not persisted across reboot

- **Affected code:** `src/native/NativeFontBridge.cpp`, `selectChoice(uint32_t index)`.
- **Trigger / reproduction:** Open the Font Family app, select a different built-in or SD-card font family, confirm that the new font takes effect, then reboot the device.
- **Observed / logically demonstrated failure:** `selectChoice()` updates `SETTINGS.fontFamily` and/or `SETTINGS.sdFontFamilyName`, calls `ensureSdFontLoaded()`, and returns `T5_FONT_OK`, but never calls `SETTINGS.saveToFile()`. Other settings paths explicitly persist through `SETTINGS.saveToFile()`. After reboot, the settings loader therefore restores the previously persisted font selection instead of the choice made through the Font Family app.
- **Likely root cause:** The native font bridge mutates the in-memory settings object but omits the persistence step used by the main settings bridge.
- **Impact:** Font selection appears successful during the current session but silently reverts after restart, making the Font Family app unreliable as a settings interface.
- **Repair direction:** Persist the updated settings before returning success. If persistence can fail, preserve the old font fields, attempt the save, and restore/reload the previous selection on failure so the API cannot report success for an uncommitted preference. Add a regression test that changes the font through `selectChoice()`, reloads settings from storage, and verifies the selected family survives.

### 5. Time Card can overwrite valid history after a store-load failure

- **Affected code:** `Apps/timecard.c`, `load_store()`, `consume_keyboard()`, `app_main()`, and write paths through `set_punch()` / `save_store()`.
- **Trigger / reproduction:** Start Time Card with an existing `/sd/.crosspoint/timecard.json`, then make `storage->read_file()` fail or provide malformed/truncated JSON so `load_store()` returns `false`. After the app continues, record or edit a punch.
- **Observed / logically demonstrated failure:** `load_store()` sets `day_count = 0` before attempting the read/parse and can return `false` after that reset, including after partially appending parsed days. Both `app_main()` and `consume_keyboard()` ignore the return value. The UI therefore continues with an empty or partial in-memory history. A later `set_punch()` calls `save_store()`, which atomically replaces the existing store with only that reduced in-memory data set.
- **Likely root cause:** Store loading is destructive and non-transactional, while callers treat a failed load as if a valid empty database had been loaded.
- **Impact:** A transient SD read failure or malformed file can turn into permanent loss of previously valid time-card history as soon as the user saves another punch.
- **Repair direction:** Parse into a temporary day array/count and commit it to the live state only after the entire store validates. Propagate load failure into a read-only/recovery state and block `save_store()` until a valid store has been loaded or the user explicitly chooses a recovery/reset action. Add tests for read failure, malformed JSON after several valid records, and subsequent punch attempts proving the original store is never replaced.

### 6. Time Card subtracts lunch intervals that occur outside the work shift

- **Affected code:** `Apps/timecard.c`, `worked(const tc_day_t *day)`.
- **Trigger / reproduction:** Create a day with Clock in = 9:00 AM, Lunch start = 7:00 AM, Lunch end = 8:00 AM, and Clock out = 5:00 PM. Open the day/week view and inspect the worked duration.
- **Observed / logically demonstrated failure:** `worked()` starts with `clock_out - clock_in`, then subtracts any lunch interval satisfying only `lunch_start >= 0` and `lunch_end >= lunch_start`. It does not require the lunch interval to overlap the shift. The example therefore reports 7:00 worked even though the 7:00–8:00 AM lunch occurred entirely before the 9:00 AM clock-in and the correct shift duration is 8:00.
- **Likely root cause:** Lunch validation checks only the lunch interval's internal ordering, not its relationship to clock-in/clock-out.
- **Impact:** Out-of-order manual edits can silently produce incorrect daily and weekly worked-time totals, including undercounting time for lunch intervals before or after the shift.
- **Repair direction:** Either reject chronologically inconsistent punch sets at edit/save time or compute lunch deduction from the intersection of `[lunch_start, lunch_end]` with `[clock_in, clock_out]`. At minimum require `clock_in <= lunch_start <= lunch_end <= clock_out` before subtracting the full lunch duration. Add regression cases for lunch before the shift, after the shift, partially overlapping the shift, and a normal in-shift lunch.

## Duplicate check performed for this scan

These three defects were checked against the existing entries above and the current open PR set on `master`. The open PRs at scan time were #204 (touch-provider migration), #194 (global Home shortcuts), and #96 (U1 implementation); none describe these defects. Targeted pull-request searches for font-selection persistence and Time Card store/lunch handling also returned no matching existing work.


## 2026-09-26 scan — 12:10 MDT

### 7. Text Editor "Discard" leaves the edited buffer in memory but marks it clean

- **Affected code:** `Apps/text_editor.c`, `key_press()` in `UNSAVED` mode, plus the transition flow through `transition()` and `continue_after()`.
- **Trigger / reproduction:** Open an existing document, make edits, press Ctrl+N or Ctrl+O (or otherwise trigger an unsaved-changes prompt), choose **D discard**, then cancel the subsequent New/Open operation and return to the editor.
- **Observed / logically demonstrated failure:** The discard path is `document.dirty = false; continue_after();`. It clears only the dirty flag; it does not restore the last saved contents, reset the document, or otherwise discard the edited buffer. If the pending New/Open operation is cancelled, the user returns to the same edited text, but it is now marked clean. Exiting no longer warns, and a later save can persist edits that the user explicitly chose to discard.
- **Likely root cause:** The implementation treats "discard" as "suppress the dirty warning" rather than restoring the pre-edit document state or committing the requested transition immediately.
- **Impact:** The editor can silently preserve and later save changes the user explicitly discarded, violating the core semantics of the unsaved-changes prompt.
- **Repair direction:** Keep a recoverable clean snapshot for the current file, or reload the current file from storage before continuing after **Discard**. For an untitled document, reset the buffer. Do not clear `document.dirty` unless the in-memory document actually matches a clean state. Add a regression test covering edit -> Ctrl+O/Ctrl+N -> Discard -> cancel pending action -> verify original content and clean state are restored.

### 8. Font Manager's two-step removal confirmation can be satisfied by one held Confirm press

- **Affected code:** `Apps/font_manager.c`, `app_main()` and `activate_selected()`; input semantics originate from `src/native/NativeAppHost.cpp::poll()`.
- **Trigger / reproduction:** Select an installed font family that has no update available. Press and hold Confirm long enough to span more than one 50 ms poll iteration.
- **Observed / logically demonstrated failure:** `app_main()` calls `activate_selected()` whenever `input.buttons & T5_APP_BUTTON_CONFIRM` is true. `NativeAppHost::poll()` populates that bit from the current pressed state, not a one-shot edge. On the first poll, `activate_selected()` sets `pending_delete = selected_index` and renders "Press Remove again". On the next poll while the same physical press is still held, the same level-triggered Confirm bit calls `activate_selected()` again, sees `pending_delete == selected_index`, and deletes the family. One sustained press can therefore satisfy both confirmation steps.
- **Likely root cause:** A level-triggered button state is used for a two-action confirmation flow without release/edge gating between actions.
- **Impact:** Font families can be deleted without the intended second deliberate confirmation press.
- **Repair direction:** Edge-detect Confirm in Font Manager (track previous button state and act only on rising edges), or require a full Confirm release after arming deletion before accepting the second press. Touch confirmation should retain equivalent two-step semantics. Add a regression test where Confirm remains asserted across multiple polls and verify deletion does not occur until a release followed by a new press.

### 9. Font Manager misses available updates when a changed font file keeps the same byte size

- **Affected code:** `src/native/NativeFontBridge.cpp`, `refreshCatalog()`, specifically installed-family update detection.
- **Trigger / reproduction:** Publish a newer catalog entry for an installed `.cpfont` file whose contents and `crc32` changed but whose byte length is identical to the installed file. Refresh Manage Fonts.
- **Observed / logically demonstrated failure:** The catalog parser records both `entry.size` and `entry.crc32`, and installation validates CRC through `computeCrc32()`. However, `refreshCatalog()` marks `family.hasUpdate` only when an installed file is missing or `installedFile.fileSize() != entry.size`. If changed content has the same size, `hasUpdate` remains false and the UI reports the family as merely "Installed" instead of offering the update.
- **Likely root cause:** Update detection uses file size as the sole content-version check even though the manifest already provides a content checksum.
- **Impact:** Legitimate font updates can be hidden indefinitely when replacement files are size-preserving, leaving users on stale or corrected font data with no visible update path.
- **Repair direction:** After the cheap existence/size check, compute the installed file CRC32 and compare it with `entry.crc32`; set `hasUpdate` on mismatch or CRC-read failure. Add regression tests for same-size/different-CRC, same-size/same-CRC, and different-size cases.

## Duplicate check performed for this scan

These defects were verified against the current `bugs.md`, the current open PR set (#208, #204, #194, and #96), and targeted all-state pull-request searches for Text Editor discard handling, Font Manager delete confirmation, and font update checksum detection. No existing tracked bug or matching PR was found for these three cases.


## 2026-09-26 scan — 13:21 MDT

### 10. App Store can act on stale release indices after a failed catalog refresh

- **Affected code:** `Apps/app_store.c`, the view transition into the releases/catalog screen, `refresh_releases()`, and the release-row activation path using `release_indices[]`.
- **Trigger / reproduction:** Open App Store, first visit a screen that populates the shared row buffers from SD/inbox packages, then switch to the online releases/catalog view while forcing the online catalog refresh to fail (network/TLS/catalog error).
- **Observed / logically demonstrated failure:** The code changes the active view to the releases screen before a successful refresh has rebuilt that view's row/index state. If `refresh_releases()` fails, the shared row buffer/count can still describe the previously rendered SD/inbox entries, while subsequent release-mode actions resolve through stale `release_indices[]`. The visible row and the release object acted on can therefore diverge.
- **Likely root cause:** The view/state transition is not transactional: UI mode changes before the new view's backing data has been successfully populated, and failure does not restore/clear the old shared state.
- **Impact:** A user can select a row that visually represents one package while App Store attempts an operation on a different release entry, producing incorrect install/update behavior or misleading failures.
- **Repair direction:** Build the release catalog and index mapping into temporary state first and only switch `view`/publish rows after success. On failure, retain the previous view or explicitly clear row/index state and render an error-only screen. Add a regression test that seeds inbox rows, forces release refresh failure, and proves no release action is possible from stale indices.

### 11. Rom Manager's ROM actions modal does not support touch row selection

- **Affected code:** `Apps/rom_manager.c`, the nested three-row ROM actions loop inside `app_main()`.
- **Trigger / reproduction:** Open **My ROMs**, select a ROM, open its actions menu, then tap **Rename**, **Delete**, or **Cancel** directly on the touchscreen.
- **Observed / logically demonstrated failure:** The nested actions loop handles `T5_UI_EVENT_PREVIOUS`, `T5_UI_EVENT_NEXT`, `T5_UI_EVENT_CONFIRM`, Back, and Exit, but has no `T5_UI_EVENT_TAP` branch and never calls `ui->hit_test()`. This differs from the outer Rom Manager lists, which do implement tap selection. Tapping an action row therefore cannot select/activate that row.
- **Likely root cause:** The modal actions loop was implemented as button-only navigation and did not reuse the standard list touch handling used elsewhere in the app.
- **Impact:** Touch users can navigate the main Rom Manager UI but become blocked or forced to use hardware/controller buttons in the per-ROM actions workflow.
- **Repair direction:** Add tap handling to the modal: call `ui->hit_test()`, validate `0 <= hit < 3`, update `a`, and either activate on the tapped selected row or mirror the main-list tap semantics. Add a touch regression test for Rename/Delete/Cancel.

### 12. Wi-Fi Settings accepts any pending Wi-Fi result without validating its request cookie

- **Affected code:** `Apps/wifi_settings.c`, `app_main()` around `system_ui->wifi_take_result()`.
- **Trigger / reproduction:** Arrange for a pending Wi-Fi UI result from another native-app workflow to exist before launching Wi-Fi Settings, with a cookie different from `WIFI_COOKIE`. Launch Wi-Fi Settings.
- **Observed / logically demonstrated failure:** `wifi_take_result(&connected, &cancelled, &cookie)` returns the pending result and clears it, but Wi-Fi Settings never checks whether `cookie == WIFI_COOKIE`. Any unread Wi-Fi result is therefore treated as if it came from this app's own selector request, so Wi-Fi Settings can skip opening its selector and show unrelated connected/cancelled state.
- **Likely root cause:** The continuation cookie is collected but ignored, defeating the correlation mechanism provided by `T5SystemUiApi`.
- **Impact:** Cross-workflow state can leak into Wi-Fi Settings, producing incorrect status and consuming another workflow's completion result.
- **Repair direction:** Validate the returned cookie before accepting the result. If it does not match `WIFI_COOKIE`, discard/route it appropriately and issue a fresh `wifi_request(WIFI_COOKIE)` rather than rendering it as this app's result. Add tests for matching and mismatched cookies.


## 2026-09-27 scan — 09:24 MDT

### 13. Legacy Recent Books v2 migration loses record alignment and corrupts later entries

- **Affected code:** `src/RecentBooksStore.cpp`, `RecentBooksStore::loadFromBinaryFile()`, specifically the `version == 2` migration branch.
- **Trigger / reproduction:** Boot with a genuine version-2 `/.crosspoint/recent.bin` containing two or more recent-book records. A v2 record serializes `path`, `title`, `author`, and `progress` for every book. This is especially easy to demonstrate when the first path still resolves to a readable book whose metadata can be reconstructed.
- **Observed / logically demonstrated failure:** The current v2 reader always consumes `path`, but consumes stored `title` and `author` only when live metadata lookup returns an empty title/author, and it never consumes the serialized `progress` field at all. Therefore the file cursor is left inside the first record. The next loop iteration interprets leftover title/author/progress bytes as the next path record, so later entries are corrupted, omitted, or migration fails.
- **Likely root cause:** The migration code decides whether to read serialized fields based on reconstructed metadata instead of first consuming the complete historical on-disk record shape; the legacy v2 `progress` field was also dropped without advancing past it.
- **Impact:** Upgrading from firmware that wrote v2 recent-book state can destroy the logical recent-books list during migration, with only the first entry potentially surviving correctly.
- **Repair direction:** For v2, unconditionally deserialize the full historical record (`path`, `title`, `author`, `progress`) into temporaries before deciding which title/author values to keep. Validate every read and parse into temporary state before replacing `recentBooks`. Add a fixture containing at least two v2 entries, including one whose live metadata lookup succeeds, and verify both records remain aligned after migration.

### 14. WebDAV overwrite paths delete the existing destination before the replacement is safely published

- **Affected code:** `src/network/WebDAVHandler.cpp`: `WebDAVHandler::raw()` at `RAW_END` for PUT, plus overwrite handling in `handleMove()` and `handleCopy()`.
- **Trigger / reproduction:** Overwrite an existing file through WebDAV, then force the replacement publication step to fail. Examples include an SD/filesystem failure when renaming a completed `.davtmp` during PUT, a failed source rename during MOVE, or destination-create/write failure during COPY.
- **Observed / logically demonstrated failure:** PUT uploads to a temporary file, but when replacing an existing target it calls `Storage.remove(_putPath)` before attempting `tmp.rename(_putPath)`. MOVE and COPY similarly remove an existing destination before the source rename or replacement copy has succeeded. If the subsequent operation fails, the old destination has already been destroyed; PUT even contradicts its own comment that the temp file is intended to avoid destroying the original on failed upload.
- **Likely root cause:** Overwrite is implemented as destructive delete-then-publish rather than a transactional replace with rollback.
- **Impact:** A transient SD/filesystem error during an overwrite can turn a failed WebDAV operation into irreversible loss of the previously valid destination file.
- **Repair direction:** Publish replacements transactionally. Rename the old destination to a bounded backup, publish the new file, then delete the backup only after success; restore the backup if publication fails. Use the same helper for PUT/MOVE/COPY so all overwrite paths have identical rollback semantics. Add fault-injection tests at each post-backup failure point proving the original file survives.

### 15. ZIP reader rejects standards-compliant archives whose EOCD comment is longer than about 1 KB

- **Affected code:** `lib/ZipFile/ZipFile.cpp`, `ZipFile::loadZipDetails()`.
- **Trigger / reproduction:** Open a valid ZIP whose End of Central Directory record has a comment longer than 1002 bytes. ZIP permits an EOCD comment up to 65,535 bytes, so such an archive is standards-compliant.
- **Observed / logically demonstrated failure:** `loadZipDetails()` scans only the final 1024 bytes of the file for the EOCD signature. The minimum EOCD record itself is 22 bytes, so once the comment exceeds 1002 bytes the EOCD signature lies outside that scan window and the function deterministically reports `EOCD signature not found in zip file`. Any feature using this ZIP reader then rejects the archive before central-directory parsing.
- **Likely root cause:** The implementation assumes the EOCD must be within the last 1 KB rather than honoring the ZIP format's 16-bit comment-length allowance.
- **Impact:** Valid ZIP-based content can fail to open or extract solely because it carries a legal archive comment; this can affect generic archive operations and any ZIP-backed content path using `ZipFile`.
- **Repair direction:** Search at least `22 + 65535` bytes from EOF (bounded by file size), scan backward for EOCD, and validate the candidate's comment length and central-directory bounds before accepting it. Add tests at comment lengths 0, 1002, 1003, and 65535 bytes.

## Duplicate check performed for this scan

These defects were checked against the current `bugs.md`, the current open issue/PR set (none returned at scan time), and targeted all-state issue/PR searches for Recent Books v2 migration, WebDAV overwrite publication, and ZIP EOCD/comment handling. No existing tracked bug or matching PR was found for these three cases.
