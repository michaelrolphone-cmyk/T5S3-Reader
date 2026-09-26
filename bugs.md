# Bug Log

## 2026-09-26 scan

### 1. Battery Status drops the minus sign for temperatures from -0.1 C through -0.9 C

- **Status:** Open PR #205 on branch `fix/battery-negative-subdegree-temperature` targeting `master`; fix implemented, focused regression-tested, and Battery Status version bumped from `1.0.0` to `1.0.1` in the same PR.
- **Affected code:** `Apps/battery.c`, `add_temperature(uint16_t deci_kelvin)`.
- **Trigger / reproduction:** Supply battery telemetry where `temperature_dk` converts to a deci-Celsius value between `-1` and `-9` (for example, `2730` deci-kelvin produces `deci_c == -1`). Open Battery Status and inspect the Temperature row.
- **Observed / logically demonstrated failure:** The formatter computes `deci_c / 10` and a separately absolute-valued fractional digit. In C integer division truncates toward zero, so `-1 / 10` is `0`; the screen therefore renders `0.1 C` instead of `-0.1 C`. The same sign loss occurs through `-0.9 C`.
- **Likely root cause:** Sign handling is split across the integer and fractional pieces, but the integer piece becomes zero for magnitudes below 1 degree.
- **Impact:** Cold battery telemetry near freezing is displayed with the wrong sign, which can mislead diagnostics and temperature-related charging decisions.
- **Repair direction:** Format the sign independently from the magnitude (for example, convert to an absolute deci-C magnitude after recording `deci_c < 0`) and render `\"%s%d.%d C\"`. Add regression cases for `-0.1 C`, `-0.9 C`, `-1.0 C`, `0.0 C`, and positive values.

### 2. Wi-Fi Settings can move selection back to row 0 without redrawing the highlight

- **Affected code:** `Apps/wifi_settings.c`, input handling in `app_main()` for `T5_APP_BUTTON_UP` / `T5_APP_BUTTON_LEFT`.
- **Trigger / reproduction:** Complete the firmware Wi-Fi selector so the result screen is shown. Move selection down/right to **Choose another network** (row 1), then press Up or Left once.
- **Observed / logically demonstrated failure:** `selected = ui->previous_index(selected, 2)` changes the logical selection from row 1 to row 0, but the code redraws only inside `if (selected != 0)`. No render occurs when the new selection is row 0, so the display continues to show row 1 as selected even though subsequent button logic considers row 0 selected.
- **Likely root cause:** The redraw is incorrectly conditional on the selected index rather than on whether navigation occurred.
- **Impact:** Visual focus and logical focus diverge. A user can see **Choose another network** highlighted while Confirm does nothing because the internal selection is actually the status row.
- **Repair direction:** Always rerender after Up/Left navigation, matching the Down/Right path. Prefer extracting the duplicated row/chrome construction into the existing result-render helper with a selected-index argument so both directions use one rendering path. Add a navigation regression test for `1 -> 0`.

### 3. Rom Manager title-detail Up/Down scrolling is reversed

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
