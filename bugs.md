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


## 2026-09-27 scan — 04:24 MDT

### 13. Mahjong hard-codes a 960×540 landscape UI onto the default portrait native-app surface

- **Affected code:** `Apps/mahjong.c`, especially the fixed geometry constants, `render_game()`, `hand_tile_at()`, and the NEW-button hit test in `app_main()`; runtime geometry comes from `src/native/NativeAppHost.cpp::width()/height()` and `GfxRenderer::getScreenWidth()/getScreenHeight()`.
- **Trigger / reproduction:** Launch the Mahjong app normally while the native-app renderer is in its default portrait orientation. The renderer defaults to `Portrait`, and `runNativeApp()` preserves the caller's current orientation rather than switching Mahjong to landscape.
- **Observed / logically demonstrated failure:** Mahjong lays out controls as if the logical screen were always 960×540: the NEW button begins at x=780 and the 14 hand tiles span x=22 through x=926. On the T5S3 portrait native-app surface the logical width is 540, so the NEW button is completely off-screen and only the first eight hand tiles fit fully on-screen. The touch hit tests use the same unreachable landscape coordinates, so the hidden controls cannot be activated by touch.
- **Likely root cause:** The demo was written against the panel's physical landscape dimensions instead of the runtime logical dimensions exposed by `T5AppApi`, and its manifest does not declare or establish a landscape-only presentation mode.
- **Impact:** A normal portrait launch produces a clipped, partially unplayable game: the user cannot reach NEW and can be unable to discard tiles that sort into the hidden portion of the 14-tile hand.
- **Repair direction:** Make Mahjong derive all layout and hit rectangles from `screen_width()/screen_height()` and provide a portrait layout, or introduce an explicit supported orientation handoff and transform input consistently. Add a regression check that every interactive rectangle and every hand slot lies within the runtime viewport for the normal launch orientation.

### 14. Image Viewer rejects valid indexed BMPs that use a reduced color table

- **Affected code:** `src/native/NativeImageBridge.cpp`, `decodeBmp()` and the BMP path through `bmpInfo()` / `renderFit()`.
- **Trigger / reproduction:** Open an uncompressed 4-bit or 8-bit BMP whose BITMAPINFOHEADER sets `biClrUsed` to a nonzero value smaller than the format maximum, for example an 8-bpp BMP with `biClrUsed = 16`, a 16-entry BGRA palette, and `bfOffBits` immediately after those 16 entries.
- **Observed / logically demonstrated failure:** `bmpInfo()` accepts the file, so probing reports a valid BMP. `decodeBmp()`, however, ignores `biClrUsed` and unconditionally calculates `paletteCount = 1 << bpp`. For the valid 8-bpp/16-color example it demands space for 256 palette entries before `bfOffBits`; the bounds check therefore returns false and Image Viewer reports a decode failure.
- **Likely root cause:** The indexed-BMP decoder assumes every 1/4/8-bpp BI_RGB file stores the full maximum palette instead of honoring the DIB header's color-table count.
- **Impact:** Standards-compliant indexed BMPs produced with compact palettes cannot be displayed even though their metadata probes successfully, creating a probe/render inconsistency and unnecessary image incompatibility.
- **Repair direction:** For DIB headers that include `biClrUsed`, use that value when nonzero and otherwise fall back to `1 << bpp`; reject counts above the format maximum, validate the actual palette bytes against `bfOffBits`, and reject any pixel index outside the declared palette. Add fixtures for reduced and full 4/8-bpp palettes.

### 15. Legacy binary string deserialization trusts corrupt length fields and can exhaust memory during boot migration

- **Affected code:** `lib/Serialization/Serialization.h::readString(FsFile&, std::string&)` and `readPod(FsFile&, T&)`; callers include `src/RecentBooksStore.cpp::loadFromBinaryFile()` and `src/WifiCredentialStore.cpp::loadFromBinaryFile()`.
- **Trigger / reproduction:** Leave no JSON replacement file and place a truncated or corrupted legacy `/.crosspoint/recent.bin` or `/.crosspoint/wifi.bin` on the SD card. For a deterministic case, encode a valid legacy version/count followed by a string length such as `0x7fffffff` with no corresponding payload, then boot.
- **Observed / logically demonstrated failure:** `readString(FsFile&,...)` reads a 32-bit length without checking the number of bytes returned, immediately calls `std::string::resize(len)`, and then ignores the payload read count. A huge on-disk length therefore requests an impossible allocation on the ESP32-S3 before the file can be rejected. If the four-byte length itself is truncated, `len` is only partially initialized and can become an arbitrary allocation size. The store loaders cannot detect the failure because these serialization helpers return `void`. Recent-books migration is invoked from normal boot via `RECENT_BOOKS.loadFromFile()`.
- **Likely root cause:** The legacy binary helpers were written for trusted, well-formed files and provide neither exact-read validation nor a caller-supplied maximum string length before resizing.
- **Impact:** Ordinary SD corruption or a malformed legacy state file can cause heap exhaustion/abort and potentially a repeatable boot failure instead of a recoverable migration error; shorter malformed reads can also populate corrupted in-memory store data.
- **Repair direction:** Make binary reads return success/failure, require exact byte counts, bound every string length before allocation using per-field limits and/or remaining file size, and make each migration abort without renaming or rewriting the legacy source on any failed read. Add regression files with truncated length words, truncated payloads, and oversized lengths and verify clean failure without allocation spikes.

## Duplicate check performed for this scan

The three findings above were checked against the current `bugs.md`, the current open-issue set (none), and the current open PRs (#241, #220, #194, and #96). PR #241 concerns installed-provider capability validation for Model Viewer; #220 is the display-abstraction foundation; #194 handles global Home double-taps; #96 is the U1 implementation branch. Targeted all-state PR searches for Mahjong orientation/geometry, indexed-BMP palette handling/`biClrUsed`, and legacy `readString` length validation returned no matching work.
