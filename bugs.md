# Bug Log

Consolidated on 2026-09-30 against master `5ec0695add0d1ea82c1278026af84f7f47bd3719`, through the 13:53 MDT scan. This is the canonical report inventory. Existing IDs 5 and 13–72 are preserved; new distinct reports receive IDs 73–210. Scan-local numbers are not canonical IDs. Entries preserve source-review reproduction evidence and repair directions; they are reported open, not claims of fresh hardware reproduction or a complete revalidation of every intervening feature change.

**199 distinct active reports: 61 carried forward and 138 newly consolidated.** The eight previous scheduled fixes (IDs 4, 6–12) are already on master: PR #244 was incorporated into [merged PR #246](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/246), including its final build-test repair `c0b3391184a2a47b034e2a2250f9dc704d2fb3c5`. They are not pending fixes. Earlier fixes #205, #209 and #212 remain excluded.

## Coverage and recovery

- This pass inspected 73 scan branches after the previous 2026-09-27 15:24 MDT cutoff and read 132 new Drive handoffs. Every changed scan branch changes only `bugs.md`; no scheduled code fixes remain to consolidate. The fix-next-bug automation is disabled and its last recorded run was 2026-09-27.
- Collapsed repeated font transaction/catalog failures, PNG validation, KOReader hashing, Wi-Fi persistence, storage publication, directory scanning, parsing, and provider-cleanup reports. Distinct functions or failure modes remain separate where they require different repairs. Immutable source links and Drive handoffs preserve the scan-local IDs and complete evidence.
- Six intermediate branches contain no changes: `20260929-0127`, `20260929-0623`, `20260929-1626`, `20260929-2046`, `20260930-0218`, `20260930-0247`. Populated findings/final or subsequent branches provide their available reports. Empty Drive artifacts (Sep 29 15:21 Diff and duplicate 21:20 Instructions/Diff) contribute no invented findings; their populated branch/companion artifacts are retained.
- Excluded the Sep 29 13:25 branch's incidental encoding damage to pre-existing reports; used its intact final report content. Original branches and Drive files are retained as provenance.
- The previous pass covered 28 scan branches and 92 handoffs, including recovered uncommitted findings from Sep 26 17:30/18:23/19:21 and Sep 27 06:24/10:22/12:21. Its canonical IDs and existing source links remain intact. Empty Sep 26 14:21/15:21/17:24 runs did not contribute fabricated findings.

## Active reports

### 5. Time Card can overwrite valid history after a store-load failure

- **Status:** Open.
- **Sources:** pre-consolidation [master](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/6d876ae443d873d06068ae866fa4eda403b95f90/bugs.md)

- **Affected code:** `Apps/timecard.c`, `load_store()`, `consume_keyboard()`, `app_main()`, and write paths through `set_punch()` / `save_store()`.
- **Trigger / reproduction:** Start Time Card with an existing `/sd/.crosspoint/timecard.json`, then make `storage->read_file()` fail or provide malformed/truncated JSON so `load_store()` returns `false`. After the app continues, record or edit a punch.
- **Observed / logically demonstrated failure:** `load_store()` sets `day_count = 0` before attempting the read/parse and can return `false` after that reset, including after partially appending parsed days. Both `app_main()` and `consume_keyboard()` ignore the return value. The UI therefore continues with an empty or partial in-memory history. A later `set_punch()` calls `save_store()`, which atomically replaces the existing store with only that reduced in-memory data set.
- **Likely root cause:** Store loading is destructive and non-transactional, while callers treat a failed load as if a valid empty database had been loaded.
- **Impact:** A transient SD read failure or malformed file can turn into permanent loss of previously valid time-card history as soon as the user saves another punch.
- **Repair direction:** Parse into a temporary day array/count and commit it to the live state only after the entire store validates. Propagate load failure into a read-only/recovery state and block `save_store()` until a valid store has been loaded or the user explicitly chooses a recovery/reset action. Add tests for read failure, malformed JSON after several valid records, and subsequent punch attempts proving the original store is never replaced.

### 13. Serial Monitor loses active line coding across keyboard handoff

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260926-2025](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260926-2025/bugs.md)

- **Affected code:** `Apps/serial_monitor_implementation.inc` in `app_main()`, Send, Custom baud, `acquire_serial_session()`, and keyboard result handling; `src/native/NativeSystemUiBridge.cpp` in `NativeKeyboardActivity::loop()`.
- **Trigger / reproduction:** Configure a non-default serial format, then use Send with the firmware keyboard. Also test changing parity or stop bits followed by Custom baud entry.
- **Observed / logically demonstrated failure:** Serial Monitor releases the serial session and returns for keyboard entry. The firmware later starts a fresh `app_main()`; it initializes and acquires at 115200/8/N/1 before reading the keyboard continuation. Send therefore uses the reset line coding. Custom baud changes only the baud on that reset configuration and loses the other prior settings.
- **Likely root cause:** The complete `t5_serial_config_t` is local state and is not preserved across the keyboard continuation.
- **Impact:** Data can be sent with serial parameters different from the user's selected configuration.
- **Repair direction:** Persist the complete line coding across keyboard handoff and restore it before reacquiring. Test non-default configurations through both Send and Custom baud continuations.

### 14. Time Card records January 1, 1970 when the clock read fails

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260926-2025](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260926-2025/bugs.md)

- **Affected code:** `Apps/timecard.c` in `today()`, `now_minutes()`, and `punch_today()`.
- **Trigger / reproduction:** Make `system_api->local_datetime()` return false, then activate Clock in, Lunch start, Lunch end, or Clock out.
- **Observed / logically demonstrated failure:** `today()` returns `19700101` on failure and `now_minutes()` returns `0`. `punch_today()` treats both as real values and can persist a punch for January 1, 1970 at 12:00 AM.
- **Likely root cause:** Clock-read failure is represented by valid date/time values rather than propagated to the caller.
- **Impact:** A transient clock failure can silently create a bogus historical punch and corrupt time-card history/totals.
- **Repair direction:** Read one `t5_local_datetime_t` snapshot and refuse the punch if it fails. Derive both date and minutes from the successful snapshot, and test that failed clock reads create no record.

### 15. App Store silently hides applications after row 64

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260926-2025](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260926-2025/bugs.md); [automation/bug-scan-20260927-0725](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0725/bugs.md)

- **Affected code:** `Apps/app_store.c`: `MAX_ROWS`, `build_releases()`, and `build_inbox()`; online catalog capacity in `src/native/NativeAppHost.cpp`.
- **Trigger / reproduction:** Load more than 64 valid application releases or place more than 64 valid app package directories in `/sd/Packages/Inbox`.
- **Observed / logically demonstrated failure:** App Store has fixed 64-row storage and stops adding entries at that limit. The firmware catalog can expose up to 128 assets. There is no pagination or truncation indicator, so later valid applications are invisible.
- **Likely root cause:** A fixed render buffer is also used as the complete catalog model.
- **Impact:** Valid applications beyond the first 64 cannot be discovered, installed, updated, or managed through App Store.
- **Repair direction:** Page or virtualize over the provider's full count and keep a catalog index per visible row. Add tests at 65 and 128 online entries and more than 64 inbox entries.

### 16. Button Remap applies a failed mapping to the live input system even when persistence fails

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260926-2120](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260926-2120/bugs.md); [automation/bug-scan-20260927-0824](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0824/bugs.md)

- **Affected code:** `src/native/NativeButtonRemapBridge.cpp`, `applyMapping(const t5_button_remap_mapping_t *mapping)` and `resetDefaults()`; `src/MappedInputManager.cpp`, `mapButton()`; `Apps/button_remap.c`, the failed-save retry path in `app_main()`.
- **Trigger / reproduction:** Open **Remap Front Buttons**, complete a valid four-button mapping while forcing `SETTINGS.saveToFile()` to fail. The app reports **Could not save button mapping** and remains on the final mapping step. The same defect occurs when **Reset** fails to persist.
- **Observed / logically demonstrated failure:** `applyMapping()` writes all four `SETTINGS.frontButton*` fields before calling `SETTINGS.saveToFile()`, then simply returns that save result. On failure, the live fields are never restored. `MappedInputManager::mapButton()` reads those live fields on every input query, so the device immediately begins using the mapping that the UI just reported as unsaved. The remap app's retry decoder still translates logical input through the pre-edit `original` mapping, so subsequent physical presses can be identified as the wrong hardware buttons. A reboot restores the old persisted mapping, producing another unexpected mapping change.
- **Likely root cause:** The bridge treats settings mutation and persistence as separate operations without rollback, while the input mapper consumes the mutable settings object directly.
- **Impact:** A storage failure can leave the current session using an uncommitted button layout, corrupt the retry workflow, and make controls behave differently before and after reboot despite the app reporting that the save failed.
- **Repair direction:** Make `applyMapping()` transactional: save the previous four button fields, apply the candidate, call `saveToFile()`, and restore the previous fields before returning `false` if persistence fails. Apply the same behavior to reset-to-defaults through the shared path. Add a regression test that forces save failure and verifies both `SETTINGS` and mapped physical/logical button behavior remain unchanged.

### 17. Button Remap advertises Reset and Cancel on front buttons but handles those actions only on the side buttons

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260926-2120](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260926-2120/bugs.md)

- **Affected code:** `Apps/button_remap.c`, `render()` and the main input loop; `src/native/NativeUiBridge.cpp`, `drawChrome()`; `src/MappedInputManager.cpp`, `mapLabels()` and `mapButton()`.
- **Trigger / reproduction:** Open **Remap Front Buttons** and use the on-screen button hints. Press the front button labelled **Reset** or **Cancel**.
- **Observed / logically demonstrated failure:** The app supplies `.previous_label = "Reset"` and `.next_label = "Cancel"`. `NativeUiBridge::drawChrome()` passes those labels to `MappedInputManager::mapLabels()`, which places them on the logical **Left** and **Right** front-button positions. But `button_remap.c` never handles `T5_APP_BUTTON_LEFT` or `T5_APP_BUTTON_RIGHT` as Reset/Cancel; it handles only `T5_APP_BUTTON_UP` as Reset and `T5_APP_BUTTON_DOWN` as Cancel. `MappedInputManager::mapButton()` maps Up/Down to the fixed side buttons. Therefore the controls the screen labels Reset/Cancel instead enter those physical front buttons as mapping choices, while the actual Reset/Cancel actions are on unlabeled side buttons.
- **Likely root cause:** The chrome's previous/next labels were used as if they described side-button Up/Down actions, but the native UI chrome maps previous/next to the remappable front Left/Right buttons.
- **Impact:** The remapping workflow gives false control instructions at exactly the point where button identity matters; users can accidentally assign a button when trying to cancel or reset, and the real escape/reset controls are undiscoverable.
- **Repair direction:** Do not advertise Reset/Cancel through `previous_label`/`next_label` unless the corresponding logical Left/Right inputs actually perform those actions. Either handle Left/Right as Reset/Cancel and provide a separate raw-front-button capture mechanism, or render explicit side-button instructions and leave the front-button hints blank while capturing raw physical front-button presses. Add a test that verifies every rendered control hint invokes the action named by that hint.

### 18. A malformed BMP width can overflow row-stride arithmetic and drive out-of-bounds pixel reads

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260926-2120](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260926-2120/bugs.md)

- **Affected code:** `src/native/NativeImageBridge.cpp`, `bmpInfo()`, `decodeBmp()`, and the BMP path in `renderFit()`; reachable through `Apps/image_viewer.c` for BMP files.
- **Trigger / reproduction:** Open a crafted uncompressed BMP whose signed width is a very large positive value but whose file contains only a tiny pixel row. For example, a 32-bpp BMP can use width `0x20000001`, height 1, a normal pixel-data offset, and only a few bytes of pixel data.
- **Observed / logically demonstrated failure:** `bmpInfo()` accepts any positive 32-bit width. `decodeBmp()` then computes `rowBytes` as `((info.width * bpp + 31u) / 32u) * 4u` using 32-bit unsigned arithmetic. With width `0x20000001` and 32 bpp, `info.width * bpp` wraps to `32`, so `rowBytes` becomes only 4 bytes and the file-size guard can pass for a tiny file. The subsequent loop still iterates to the original huge `info.width` and, for 32-bpp data, reads `row + sx * 4`, quickly running beyond the loaded file buffer. This can fault or watchdog-reset the device when the BMP is rendered.
- **Likely root cause:** Image dimensions are accepted without a sane bound, and packed-row size is calculated in overflow-prone 32-bit arithmetic before the bounds check.
- **Impact:** A malformed or corrupted BMP copied to the SD card can crash the Image Viewer/device instead of being rejected as invalid input.
- **Repair direction:** Calculate bits-per-row and row stride in checked 64-bit arithmetic, reject dimensions/stride values that overflow `size_t` or exceed practical decoder limits, and prove `dataOffset + stride * height <= file.size` before entering the pixel loops. Add malformed-BMP regression fixtures covering width×bpp overflow, oversized dimensions, truncated rows, and valid boundary cases.

### 19. Status Bar settings can cycle repeatedly from one held Confirm press

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260926-2221](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260926-2221/bugs.md)

- **Affected code:** `Apps/status_bar_settings.c`, `app_main()`; `src/native/NativeAppHost.cpp`, `poll()`; `src/native/NativeStatusBarBridge.cpp`, `itemActivate()`.
- **Trigger / reproduction:** Open **Customize Status Bar**, select any item, then press and hold Confirm for more than one 50 ms polling interval.
- **Observed / logically demonstrated failure:** `NativeAppHost::poll()` exports button levels using `MappedInputManager::isPressed()`. Status Bar Settings calls `statusbar->item_activate()` on every loop where the Confirm bit is still set, without edge or release gating. A held press therefore toggles binary settings repeatedly and rapidly cycles three-state settings such as Progress Bar, Thickness, Title, and Clock. `itemActivate()` also calls `SETTINGS.saveToFile()` on every cycle, so one physical press can cause several persistence writes. The final setting depends on how many polling iterations the button remains down rather than on a single deliberate action.
- **Likely root cause:** The app treats the native app ABI's level-triggered button state as a one-shot Confirm event.
- **Impact:** A user can select one value but release the button with a different value active, and a single press can generate unnecessary repeated settings writes.
- **Repair direction:** Track the previous button mask and act only on the Confirm rising edge, as Springboard already does, or move this app to the edge-based UI event API. Add a regression test that holds Confirm asserted across several polls and verifies exactly one activation/save occurs.

- **Consolidation sources:** [automation/bug-scan-20260929-0427](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/dc6897e74b7c37d55d43e6b711951cb7cc8d25e3/bugs.md); [Drive: 2026-09-29 0427 - automation-bug-scan-20260929-0427 - Instructions](https://docs.google.com/spreadsheets/d/1t59zkOqhm-R5DDAkGzYzXZV7OYMycN2a7VESAjxX4LE/edit?usp=drivesdk); [Drive: 2026-09-29 0427 - automation-bug-scan-20260929-0427 - Diff](https://docs.google.com/spreadsheets/d/1UGD9EGzpw3_BHk-0Timu5HoPY_Hvdkn-5pbCv3jVUF8/edit?usp=drivesdk)

### 20. Text Editor's Open picker silently hides documents after fixed scan limits

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260926-2221](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260926-2221/bugs.md)

- **Affected code:** `Apps/text_editor.c`, `list_files()`, `FILE_LIMIT`, and the `files` picker array.
- **Trigger / reproduction:** Put more than 64 valid `.txt`/`.md` documents in `/sd/Documents`, then use Ctrl+O. A second trigger is to place a valid text document after the first 128 directory entries while enough earlier entries are directories or unsupported filenames that fewer than 64 valid documents have been collected.
- **Observed / logically demonstrated failure:** `list_files()` stops when either `file_count == FILE_LIMIT` (64) or `seen == 128`. It provides no continuation page, cursor, or truncation warning. Valid documents beyond either boundary never enter `files[]`, so they cannot be selected through the editor's Open workflow even though they exist and are otherwise valid.
- **Likely root cause:** The initial bounded picker implementation uses fixed in-memory arrays and a separate hard cap on directory entries examined, but does not expose pagination or resume state.
- **Impact:** Larger Documents directories become partially inaccessible from Text Editor, and unrelated/non-text entries can cause valid documents to disappear even before the 64-document capacity is reached.
- **Repair direction:** Stream/paginate directory entries and retain a directory cursor or page offset instead of materializing a single bounded list. At minimum continue scanning past unsupported entries and clearly expose truncation with a Next page. Add tests for 65+ valid documents and for a valid document positioned after 128 mixed directory entries.

### 21. OPDS Add Server persists an incomplete server before a required URL exists

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260926-2221](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260926-2221/bugs.md)

- **Affected code:** `Apps/opds_settings.c`, `apply_keyboard_result()` and `persist_edit()`; `src/native/NativeOpdsBridge.cpp`, `addServer()`; `src/OpdsServerStore.cpp`, `addServer()`.
- **Trigger / reproduction:** Open **OPDS Servers** -> **Add Server**, edit **Server Name** first, enter any name, then press Back before setting **Server URL**. The same incomplete state can be created by accepting the URL field while it contains only the placeholder `https://` or `http://`, because the app normalizes that placeholder to an empty string before persisting.
- **Observed / logically demonstrated failure:** A new editor starts with an empty `edit_server`. After *any* field keyboard result, `apply_keyboard_result()` immediately calls `persist_edit()`. When `editor_is_new` is true, that calls `opds->add()` and converts the draft into a persisted server immediately. Neither `NativeOpdsBridge::addServer()` nor `OpdsServerStore::addServer()` validates that `url` is non-empty or usable. Editing the name first therefore writes an OPDS record with an empty URL; Back then leaves that invalid record installed.
- **Likely root cause:** The edit workflow conflates per-field persistence with creation of the server object, and the storage/API layer has no required-field validation.
- **Impact:** Normal field-entry order can create broken OPDS catalog entries that cannot be contacted and remain in the configured server list until manually repaired or deleted.
- **Repair direction:** Keep a new server as an in-memory draft until required fields validate and the user explicitly saves/finishes it, or at minimum reject `addServer()` while the URL is empty/invalid. Preserve per-field autosave only after the initial valid record has been created. Add tests for Name-first, placeholder-only URL, Back-before-URL, and a valid URL-first creation flow.

### 22. File Browser root destination picker overruns its directory-name backing array

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260926-2324-findings](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260926-2324-findings/bugs.md); [automation/bug-scan-20260927-1124](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-1124/bugs.md)

- **Affected code:** `Apps/file_browser.c`, `PICKER_ENTRIES`, `picker_names[]`, `picker_rows[]`, `list_picker_directories()`, and `choose_destination()`.
- **Trigger / reproduction:** Put at least 97 visible directories in the root of the destination volume, select a file, choose **Move** or **Copy**, and open the SD/USB destination picker at `/`.
- **Observed / logically demonstrated failure:** `picker_names` contains 96 elements, while `picker_rows` contains 98. At the root, `choose_destination()` inserts only the fixed “<verb> here” row, so `list_picker_directories(..., offset=1)` starts with `count == 1`. Its loop permits `count < PICKER_ENTRIES + 2`, including `count == 97`, and stores the 97th directory name at `picker_names[count - offset]`, which is index 96 and outside the 0–95 array range. The same fixed-capacity scan provides no continuation for later directories.
- **Likely root cause:** The enumeration bound is based on the larger row array instead of the smaller name backing array, while the number of fixed rows differs between root and non-root picker views.
- **Impact:** Opening the destination chooser on a valid root containing 97 or more folders invokes undefined behavior and can overwrite unrelated app state. Directories beyond the fixed window are also unavailable as destinations.
- **Repair direction:** Bound directory enumeration by `count - offset < PICKER_ENTRIES`, or make the row/name capacities explicitly consistent. Prefer pagination so every destination remains reachable. Add guarded or sanitizer-backed tests for 96, 97, and more than 98 root directories, plus a non-root picker containing the `..` row.

### 23. File Browser silently hides registered Open-with handlers after the first eight

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260926-2324-findings](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260926-2324-findings/bugs.md)

- **Affected code:** `Apps/file_browser.c`, `MAX_OPEN_HANDLERS` and `choose_handler()`; `src/native/NativeFileOpenBridge.cpp`, `handlerCount()` and `handlerGet()`; `src/native/FileAssociationRegistry.h` and `src/native/FileAssociationRegistry.cpp`.
- **Trigger / reproduction:** Install more than eight valid applications that register the same supported file extension, then select a file with that extension in File Browser and open the **Open with** chooser.
- **Observed / logically demonstrated failure:** The native association registry permits up to 128 total handlers, and `handler_count()` reports every handler for the requested extension. File Browser immediately clamps that count to `MAX_OPEN_HANDLERS == 8` and fetches only indices 0 through 7. There is no pagination or truncation warning, so handlers 9 and later can never be selected even though the native bridge can return them.
- **Likely root cause:** The fixed local stack arrays in the chooser are treated as the complete data model instead of one page over the registry's larger bounded result set.
- **Impact:** A valid installed application can advertise support for a file type but remain unreachable from the system's normal file-opening workflow. Because the registry is sorted, adding another handler can also push a previously reachable app past the cutoff.
- **Repair direction:** Page or otherwise enumerate the full `handler_count()`, fetching visible entries with `handler_get()`. If a deliberate UI maximum remains, report it instead of silently dropping handlers. Add a regression with at least ten handlers for one extension and verify handlers beyond index 7 can be selected and launch the intended app.

### 24. Provider capability discovery ignores package directories after entry 64 and can miss ambiguity

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260926-2324-findings](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260926-2324-findings/bugs.md)

- **Affected code:** `src/native/NativeProviderCapabilityBridge.cpp`, `findProvider()`.
- **Trigger / reproduction:** Place more than 64 valid package directories in one of `/Drivers`, `/Providers`, or `/Services`, with the only provider matching a requested capability positioned after the first 64 directory entries. A second case places one matching provider before the cutoff and another matching provider after it.
- **Observed / logically demonstrated failure:** For each root, `findProvider()` executes `for (unsigned i = 0; i < 64; ++i)` and calls `openNextFile()` only inside that loop. Entry 65 and later are never examined. In the first case, capability acquisition can report “No installed provider profile matches capability” even though a valid provider is installed. In the second, it returns the earlier provider instead of noticing the later duplicate, despite the function's stated policy to reject ambiguous matches.
- **Likely root cause:** A scan-work limit was applied to filesystem position even though this resolver needs no growing result collection; it can keep memory bounded while walking the directory to exhaustion.
- **Impact:** Capability resolution becomes dependent on filesystem enumeration order and package count. Installing unrelated packages can make a valid capability disappear, and an ambiguous provider set can be accepted when it should be rejected.
- **Repair direction:** Enumerate each provider root to exhaustion while retaining only the current match and an ambiguity flag, or implement explicit resumable paging that still covers every entry. Add tests with a matching provider at position 65 or later and with two matches straddling the old cutoff. The current U1 branch requires the same repair because its `findProvider()` retains the identical 64-entry loop.

### 25. SD Firmware Update validates an empty path after a File Browser handoff

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0023](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0023/bugs.md); [automation/bug-scan-20260927-1124](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-1124/bugs.md)

- **Affected code:** `src/native/NativeSdFirmwareBridge.cpp`, `resolveSelectedPath()` and `validateSelected()`; handoff source in `src/native/NativeFileOpenBridge.cpp::activeSourceStoragePath()`; `Apps/sd_firmware_update.json`.
- **Trigger / reproduction:** From the installable File Browser, open a valid firmware `.bin` using the SD Firmware Update file association. This is the normal non-recovery flow introduced with the File Browser retirement work. The bridge has no explicit `selectedPath` setter caller in the repository, so the selected image is supplied by `NativeFileOpenBridge::activeSourceStoragePath()`.
- **Observed / logically demonstrated failure:** `validateSelected()` successfully resolves the selected image into local variable `path`, opens that resolved path, records its size, and checks the OTA partition. It then calls `firmware_flash::validateImageFile(selectedPath.c_str(), ...)` instead of `path.c_str()`. In a File Browser handoff `selectedPath` is still empty, so a valid selected image is re-opened as an empty pathname and validation returns an open/invalid-image failure. The app can display the selected filename because `selected_path()` uses `resolveSelectedPath()`, while validation still fails.
- **Likely root cause:** The bridge was migrated from an explicit firmware-owned selected-path state to the File Browser active-source fallback, but the final validator call retained the old `selectedPath` member instead of using the resolved local path.
- **Impact:** The newly migrated normal Settings/File Browser firmware-update path cannot validate a legitimate `.bin` selected through the association handoff, blocking firmware updates outside recovery mode.
- **Repair direction:** Pass `path.c_str()` to `firmware_flash::validateImageFile()` and use the resolved path consistently throughout validation/install. Add an integration test that supplies the image only through `NativeFileOpenBridge::activeSourceStoragePath()` and verifies validation reaches the real file; the existing native-app test mocks `validate()` and does not exercise this bridge path.

### 26. Language selection reports success even when the settings file was not saved

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0023](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0023/bugs.md)

- **Affected code:** `src/native/NativeLanguageBridge.cpp`, `selectLanguage(uint8_t languageId)`; caller `Apps/language_settings.c::select_current()`.
- **Trigger / reproduction:** Make settings persistence fail (for example, unavailable/unwritable settings storage), open Language, and select a different language.
- **Observed / logically demonstrated failure:** `selectLanguage()` immediately changes both `I18N` and `SETTINGS.language`, calls `SETTINGS.saveToFile()`, ignores its return value, and unconditionally returns `true`. `language_settings.c` treats that `true` as success and exits the app. The new language therefore appears active for the current session even though it was never committed; after settings are reloaded or the device reboots, the prior persisted language returns.
- **Likely root cause:** The native language bridge treats an in-memory mutation as the success condition instead of the persistence result.
- **Impact:** The Language app can falsely acknowledge a preference change and leave runtime state inconsistent with durable settings.
- **Repair direction:** Preserve the previous language, attempt persistence, and return success only after `saveToFile()` succeeds. On failure, restore both `SETTINGS.language` and the active `I18N` language before returning `false`. Add a regression test with a failing settings writer proving the API returns failure and leaves the previous language active.

### 27. A failed Time Zone save still changes the live timezone and system clock

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0023](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0023/bugs.md)

- **Affected code:** `src/native/NativeTimeZoneBridge.cpp`, `selectCity(uint32_t region, uint32_t city)`; caller `Apps/time_zone.c::activate()`.
- **Trigger / reproduction:** Make settings persistence fail, then select a different city in Time Zone.
- **Observed / logically demonstrated failure:** `selectCity()` writes the new ID into `SETTINGS.timeZoneId`, reconfigures `halClock`, synchronizes system time from the RTC, and updates `SETTINGS.rtcStoresUtc` before calling `SETTINGS.saveToFile()`. If the save fails, the function returns `false`, but none of those live mutations are rolled back. The app therefore receives a failure while the process is already operating with the new timezone/clock configuration; a later reboot can restore the old persisted zone.
- **Likely root cause:** Persistence is treated as the final step of a multi-part state change without a transaction or rollback path.
- **Impact:** A storage failure can leave displayed/system time and in-memory settings disagreeing with durable configuration, including a timezone change the UI did not successfully commit.
- **Repair direction:** Snapshot the old timezone and RTC-related settings before applying the candidate. If persistence fails, restore the old fields and reconfigure/resynchronize `halClock` to the previous zone. Prefer a staged settings transaction so durable state and live clock configuration move together. Add a failure-injection test covering both the returned error and restoration of the old timezone.

### 28. 3D Model Viewer can remain stuck on the loading frame when its first model render loses the video-submit race

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0125](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0125/bugs.md)

- **Affected code:** `Apps/model_viewer.c`, `mv_message()`, `mv_render()`, and `app_main()`; `src/native/NativeVideoBridge.cpp`, `epd_video_can_submit()` / `epd_video_submit()`.
- **Trigger / reproduction:** Open a small, fast-to-parse OBJ/STL from File Browser so model loading and touch setup complete before the asynchronous raw-EPD scan task has consumed the preceding **LOADING MODEL** frame. This is easiest to provoke with a tiny model immediately after launch.
- **Observed / logically demonstrated failure:** `mv_message("LOADING MODEL", ...)` only waits until `submit()` accepts the loading frame; it does not wait for that queued flip to be consumed. The video bridge reports `can_submit() == false` while `g_flip_req` is still set. After loading, `app_main()` explicitly sets `g_need_refine = false` and calls `(void)mv_render(false)` exactly once. If that call sees the still-pending loading flip, `mv_render()` returns `false`; the result is ignored and no idle retry is scheduled. With no subsequent touch/button interaction, the loading frame can therefore remain displayed indefinitely even though the model is loaded and the event loop is running.
- **Likely root cause:** The first model frame is treated as fire-and-forget even though the video API is intentionally nonblocking and can reject a submit while a previous flip is queued.
- **Impact:** Valid small models can appear to hang on **LOADING MODEL**, misleading the user into thinking parsing or the app has frozen; only later interaction may cause a render.
- **Repair direction:** Treat a failed initial `mv_render(false)` as pending work: leave/set `g_need_refine = true` and retry when `can_submit()` becomes available, or use `pending()` / frame-counter synchronization before the first model render. Never clear the refine/retry flag until a render actually submits successfully. Add a test/fake video API that rejects the first post-loading submit and verify the viewer retries without user input.

### 29. Web Server can emit multiple HTTP responses after a mid-file SD read failure

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0125](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0125/bugs.md)

- **Affected code:** `src/native/NativeWebServerBridge.cpp`, `streamFile(String path)` and `handleHttpRequest()`.
- **Trigger / reproduction:** Request an existing portal file and cause the SD read to fail or terminate early after at least part of the file has been read, for example from a transient card I/O failure after the HTTP headers have been sent.
- **Observed / logically demonstrated failure:** `streamFile()` sends `200`, `Content-Length`, and the response headers before streaming the file body. If a later `file.read()` returns `<= 0` before `sent == total`, the function returns `false` even though a 200 response and partial body have already been transmitted. `handleHttpRequest()` interprets `false` as “file not found”, tries a second `/index.html` path, and can finally call `send(404, ...)` on the same request. The client can receive a truncated 200 body followed by bytes or headers from another response, violating the advertised Content-Length and corrupting the HTTP transaction.
- **Likely root cause:** `streamFile()` uses one boolean to mean both “nothing was served, try another path” and “serving started but failed partway through”.
- **Impact:** SD read faults can produce malformed HTTP responses, confusing captive-portal and browser clients and potentially causing mixed response data to be parsed or cached.
- **Repair direction:** Distinguish **not opened/not found** from **response started then failed**, for example with a tri-state result. Once headers or body transmission starts, never attempt another route or send a second status for that request; terminate the client connection on a short read and log the transfer failure. Add a fault-injected short-read test that proves no fallback 404 or second response is emitted after a 200 begins.

- **Consolidation sources:** [automation/bug-scan-20260928-2354](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/adbef888b66176c0dd8e859e91629a93c1ceda23/bugs.md); [Drive: 2026-09-28 2354 automation-bug-scan-20260928-2354 diff](https://docs.google.com/document/d/1Q0TY6FxGuO5mz5-eim6LjlhC5BL6u24Ibi5DKNFyUUs/edit?usp=drivesdk); [Drive: 2026-09-28 2354 automation-bug-scan-20260928-2354 instructions](https://docs.google.com/document/d/1tPJ5PE9mW3TkqebBAGNIijAyJCz0rCccHTRJ2e4Tr50/edit?usp=drivesdk)

### 30. USB Debug log filenames collide for distinct same-model devices that expose no serial number

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0125](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0125/bugs.md)

- **Affected code:** `Apps/usb_debug.c`, `make_identifier()` and `save_report()`.
- **Trigger / reproduction:** Attach two distinct USB devices with the same VID/PID and no USB serial string. Inspect each device and choose **Save Log**.
- **Observed / logically demonstrated failure:** `make_identifier()` produces `usb_<VID>_<PID>_<serial>` only when a serial string is present; otherwise every device with that VID/PID receives exactly `usb_<VID>_<PID>`. `save_report()` then uses only that identifier in `/sd/usb-debug/<identifier>.txt`. Two simultaneously attached, physically distinct devices can therefore never receive distinct log paths: a later save targets the same file as the first, either replacing the earlier report under the storage implementation or making the second save fail if replacement is refused.
- **Likely root cause:** The human-readable VID/PID fallback is being used as a unique per-device persistence key even though VID/PID identifies a product model, not a device instance.
- **Impact:** USB-debug evidence can be lost or attributed to the wrong physical device precisely in multi-device or hub debugging, where separate reports are most important.
- **Repair direction:** Keep the VID/PID/serial prefix for readability but append a stable per-session disambiguator when serial is absent, such as the generation-qualified host token or a deterministic ordinal derived from the current device snapshot. Ensure repeated saves for one selected device use the same name while two distinct active tokens cannot collide. Add a regression test with two no-serial devices sharing VID/PID.

### 31. Settings reports success when persistence fails

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0221](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0221/bugs.md)

- **Affected:** `src/native/NativeSettingsBridge.cpp::nativeSettingsActivate()`; `Apps/settings.c`.
- **Trigger:** Change a direct toggle/enum/ranged setting while the settings file cannot be written.
- **Failure:** The live `SETTINGS` field is changed, immediate effects such as backlight are applied, `SETTINGS.saveToFile()` is called but its result is ignored, and `T5_APP_SETTING_UPDATED` is always returned. The UI shows the new value although reboot/reload can restore the old value.
- **Root cause / impact:** Mutation and persistence are not transactional, so live and durable configuration can diverge while the UI reports success.
- **Repair:** Save transactionally: retain the old value, require save success before UPDATED, restore value/side effects on failure, and test an injected save failure.

- **Consolidation sources:** [automation/bug-scan-20260928-2354](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/adbef888b66176c0dd8e859e91629a93c1ceda23/bugs.md); [Drive: 2026-09-28 2354 automation-bug-scan-20260928-2354 diff](https://docs.google.com/document/d/1Q0TY6FxGuO5mz5-eim6LjlhC5BL6u24Ibi5DKNFyUUs/edit?usp=drivesdk); [Drive: 2026-09-28 2354 automation-bug-scan-20260928-2354 instructions](https://docs.google.com/document/d/1tPJ5PE9mW3TkqebBAGNIijAyJCz0rCccHTRJ2e4Tr50/edit?usp=drivesdk)

### 32. Native storage `read_file` silently truncates files above 50,000 bytes and reports the truncated size as complete

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0221](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0221/bugs.md); [automation/bug-scan-20260927-1524](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-1524/bugs.md)

- **Affected code:** `src/native/NativePlatformBridge.cpp`, `readFile(const char *path, void *buffer, size_t capacity, size_t *outSize)`; `lib/hal/HalStorage.cpp`, `HalStorage::readFile(const char *path)`; the `T5StorageApi` contract in `lib/NativeApps/include/T5StorageApi.h`.
- **Trigger / reproduction:** Put a file larger than 50,000 bytes on the SD card and call the native storage API's `read_file()` on it. This is visible even with the documented size-probe pattern: call `read_file(path, NULL, 0, &size)` and compare `size` with the real file size. It can also be reproduced by passing a buffer large enough for the complete file and comparing the returned byte count/content with the source.
- **Observed / logically demonstrated failure:** `HalStorage::readFile()` has a hard-coded `maxSize = 50000` and stops appending once `readSize` reaches that limit, even while the file still has data. `NativePlatformBridge::readFile()` then treats the truncated `String` as the complete file, sets `*outSize = contents.length()`, and returns success. A 60,000-byte file is therefore reported as a successful 50,000-byte file; callers receive no indication that 10,000 bytes were discarded.
- **Likely root cause:** A bounded text-oriented firmware helper was reused to implement the generic native storage ABI without preserving the file's actual size or surfacing the helper's truncation limit.
- **Impact:** Native and third-party ELF apps can silently parse or persist incomplete data while believing the read succeeded. The size-probe form of the API is also unreliable for larger files, so a caller cannot even allocate the correct buffer before reading.
- **Repair direction:** Implement the bridge on `HalFile` instead of `HalStorage::readFile()`: open the file, obtain the real `fileSize64()`, report that value for size-only probes, fail cleanly when it exceeds `SIZE_MAX` or the supplied capacity, and otherwise loop until exactly that many bytes are read or an I/O error occurs. Add regression coverage with a file above 50,000 bytes for both size-only and full-buffer reads.

### 33. T5Storage stream handles survive native-app teardown

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0221](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0221/bugs.md)

- **Affected:** `src/native/NativePlatformBridge.cpp` globals `streamFile`/`writeStreamFile`; `streamOpen()`/`writeStreamOpen()`; `src/native/NativeAppHost.cpp::runNativeApp()`.
- **Trigger:** App A opens a T5Storage read or write stream and exits without close/commit/abort; App B then opens the same stream type.
- **Failure:** The process-global `HalFile` remains open, so the next open is rejected. `runNativeApp()` has no storage-platform teardown; `nativeStreamsEnd()` cleans the separate T5Stream subsystem. An unfinished writer can leave its `.part` state active.
- **Root cause / impact:** T5Storage stream globals have no per-app ownership/cleanup, so later apps can lose that stream slot until restart.
- **Repair:** Bind handles to the active app/ExecutionContext and force close/abort on teardown (or add NativePlatform begin/end hooks). Add a two-app teardown regression test.

### 34. KOReader settings remain live after a failed persistence write

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0325](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0325/bugs.md)

- **Affected code:** `src/native/NativeKOReaderBridge.cpp`, its four setting mutation functions; user-visible behavior in `Apps/koreader_sync.c`.
- **Trigger / reproduction:** Make the KOReader settings write fail, then change an account field, sync-server URL, or document-match method. The app reports **Could not save setting**. Continue using KOReader Sync in the same session, then reboot.
- **Observed / logically demonstrated failure:** Each bridge setter mutates the process-global `KOREADER_STORE` first and only then calls `saveToFile()`. If persistence fails, the setter returns `false`, but the new value remains in the live store. Subsequent settings reads and authentication therefore use/display the unsaved value even though the UI reported failure; after reboot the older persisted value is restored.
- **Likely root cause:** The setters use mutate-then-save semantics with no snapshot, rollback, or transactional staging.
- **Impact:** Runtime configuration can diverge from persistent configuration after a failed write. Authentication can run with settings the user was explicitly told were not saved, and the apparent setting later reverts after restart.
- **Repair direction:** Snapshot the affected store fields before mutation and restore them if `saveToFile()` fails, or persist a staged copy and only publish it into the live store after a successful write. Add fault-injection tests proving a failed save leaves both live and persisted values unchanged.

### 35. Legacy language migration can retire the only recoverable setting before persistence succeeds

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0325](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0325/bugs.md)

- **Affected code:** `src/CrossPointSettings.cpp`, `CrossPointSettings::migrateLanguageBinaryFile()`.
- **Trigger / reproduction:** Boot with legacy `/.crosspoint/language.bin` present. Either make the legacy file fail to open/read, or allow it to read successfully but make `settings.json` persistence fail.
- **Observed / logically demonstrated failure:** The migration attempts to read the legacy value only inside `if (Storage.openFileForRead(...))`, but regardless of whether that read succeeds it then calls `Storage.rename(LANG_FILE_BIN, LANG_FILE_BAK)`, ignores that result, calls `saveToFile()`, ignores that result, logs **Migrated language.bin into settings.json**, and returns `true`. A transient read failure can therefore retire the source without importing its value; a destination-write failure can retire the source even though the imported value was never persisted.
- **Likely root cause:** Source retirement and success reporting are unconditional instead of being committed only after a validated read and successful destination write.
- **Impact:** A one-time migration can silently lose the user's language preference and suppress an automatic retry on the next boot.
- **Repair direction:** Treat the migration transactionally: require a successful open, complete/validated legacy read, and successful `saveToFile()` before renaming the legacy file. If any step fails, leave `language.bin` intact and return `false`; also check the rename result. Add regression tests for open failure, truncated/invalid legacy data, destination-write failure, and successful migration.

### 36. Firmware Flasher silently hides firmware images after the first 64

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0325](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0325/bugs.md)

- **Affected code:** `Apps/esp_rom_flasher.c`, `MAX_IMAGES`, the fixed image inventory arrays, and the root-directory scan in `app_main()`.
- **Trigger / reproduction:** Place more than 64 qualifying ESP `.bin` or MSP430 TI-TXT `.txt` firmware images in `/sd`, then open Firmware Flasher.
- **Observed / logically demonstrated failure:** The scan loop is `while (image_count < MAX_IMAGES && app->dir_next(&entry))`. Once the 64th qualifying image is collected, directory enumeration stops entirely. Later valid firmware files are never shown and cannot be selected, and the UI gives no truncation warning or continuation path.
- **Likely root cause:** A fixed in-memory row buffer is also being used as the total directory-inventory limit.
- **Impact:** Valid firmware images can become inaccessible solely because of directory ordering, which can make the flasher appear unable to see a file that is present on the SD card.
- **Repair direction:** Separate inventory traversal from visible-page storage. Page or stream the directory with a cursor/offset, or use a bounded dynamic inventory with an explicit continuation/truncation state. Add tests with exactly 64 and more than 64 qualifying images and verify every image remains reachable.

### 37. Mahjong hard-codes a 960×540 landscape UI onto the default portrait native-app surface

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0424](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0424/bugs.md)

- **Affected code:** `Apps/mahjong.c`, especially the fixed geometry constants, `render_game()`, `hand_tile_at()`, and the NEW-button hit test in `app_main()`; runtime geometry comes from `src/native/NativeAppHost.cpp::width()/height()` and `GfxRenderer::getScreenWidth()/getScreenHeight()`.
- **Trigger / reproduction:** Launch the Mahjong app normally while the native-app renderer is in its default portrait orientation. The renderer defaults to `Portrait`, and `runNativeApp()` preserves the caller's current orientation rather than switching Mahjong to landscape.
- **Observed / logically demonstrated failure:** Mahjong lays out controls as if the logical screen were always 960×540: the NEW button begins at x=780 and the 14 hand tiles span x=22 through x=926. On the T5S3 portrait native-app surface the logical width is 540, so the NEW button is completely off-screen and only the first eight hand tiles fit fully on-screen. The touch hit tests use the same unreachable landscape coordinates, so the hidden controls cannot be activated by touch.
- **Likely root cause:** The demo was written against the panel's physical landscape dimensions instead of the runtime logical dimensions exposed by `T5AppApi`, and its manifest does not declare or establish a landscape-only presentation mode.
- **Impact:** A normal portrait launch produces a clipped, partially unplayable game: the user cannot reach NEW and can be unable to discard tiles that sort into the hidden portion of the 14-tile hand.
- **Repair direction:** Make Mahjong derive all layout and hit rectangles from `screen_width()/screen_height()` and provide a portrait layout, or introduce an explicit supported orientation handoff and transform input consistently. Add a regression check that every interactive rectangle and every hand slot lies within the runtime viewport for the normal launch orientation.

### 38. Image Viewer rejects valid indexed BMPs that use a reduced color table

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0424](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0424/bugs.md)

- **Affected code:** `src/native/NativeImageBridge.cpp`, `decodeBmp()` and the BMP path through `bmpInfo()` / `renderFit()`.
- **Trigger / reproduction:** Open an uncompressed 4-bit or 8-bit BMP whose BITMAPINFOHEADER sets `biClrUsed` to a nonzero value smaller than the format maximum, for example an 8-bpp BMP with `biClrUsed = 16`, a 16-entry BGRA palette, and `bfOffBits` immediately after those 16 entries.
- **Observed / logically demonstrated failure:** `bmpInfo()` accepts the file, so probing reports a valid BMP. `decodeBmp()`, however, ignores `biClrUsed` and unconditionally calculates `paletteCount = 1 << bpp`. For the valid 8-bpp/16-color example it demands space for 256 palette entries before `bfOffBits`; the bounds check therefore returns false and Image Viewer reports a decode failure.
- **Likely root cause:** The indexed-BMP decoder assumes every 1/4/8-bpp BI_RGB file stores the full maximum palette instead of honoring the DIB header's color-table count.
- **Impact:** Standards-compliant indexed BMPs produced with compact palettes cannot be displayed even though their metadata probes successfully, creating a probe/render inconsistency and unnecessary image incompatibility.
- **Repair direction:** For DIB headers that include `biClrUsed`, use that value when nonzero and otherwise fall back to `1 << bpp`; reject counts above the format maximum, validate the actual palette bytes against `bfOffBits`, and reject any pixel index outside the declared palette. Add fixtures for reduced and full 4/8-bpp palettes.

### 39. Legacy binary string deserialization trusts corrupt length fields and can exhaust memory during boot migration

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0424](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0424/bugs.md); [automation/bug-scan-20260927-1524](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-1524/bugs.md)

- **Affected code:** `lib/Serialization/Serialization.h::readString(FsFile&, std::string&)` and `readPod(FsFile&, T&)`; callers include `src/RecentBooksStore.cpp::loadFromBinaryFile()` and `src/WifiCredentialStore.cpp::loadFromBinaryFile()`.
- **Trigger / reproduction:** Leave no JSON replacement file and place a truncated or corrupted legacy `/.crosspoint/recent.bin` or `/.crosspoint/wifi.bin` on the SD card. For a deterministic case, encode a valid legacy version/count followed by a string length such as `0x7fffffff` with no corresponding payload, then boot.
- **Observed / logically demonstrated failure:** `readString(FsFile&,...)` reads a 32-bit length without checking the number of bytes returned, immediately calls `std::string::resize(len)`, and then ignores the payload read count. A huge on-disk length therefore requests an impossible allocation on the ESP32-S3 before the file can be rejected. If the four-byte length itself is truncated, `len` is only partially initialized and can become an arbitrary allocation size. The store loaders cannot detect the failure because these serialization helpers return `void`. Recent-books migration is invoked from normal boot via `RECENT_BOOKS.loadFromFile()`.
- **Likely root cause:** The legacy binary helpers were written for trusted, well-formed files and provide neither exact-read validation nor a caller-supplied maximum string length before resizing.
- **Impact:** Ordinary SD corruption or a malformed legacy state file can cause heap exhaustion/abort and potentially a repeatable boot failure instead of a recoverable migration error; shorter malformed reads can also populate corrupted in-memory store data.
- **Repair direction:** Make binary reads return success/failure, require exact byte counts, bound every string length before allocation using per-field limits and/or remaining file size, and make each migration abort without renaming or rewriting the legacy source on any failed read. Add regression files with truncated length words, truncated payloads, and oversized lengths and verify clean failure without allocation spikes.
- **Additional affected consumer:** `lib/KOReaderSync/KOReaderCredentialStore.cpp::loadFromBinaryFile()` uses the same unchecked helpers for `/.crosspoint/koreader.bin`. A valid version byte followed by an oversized username length or truncated payload can exhaust memory or be accepted as credentials. Bound credential fields, validate the match-method enum, and commit temporary values/retire the source only after complete validation and a successful JSON save.

### 40. Fast-video blocking flip can deadlock forever after a scan transmit failure

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0520](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0520/bugs.md)

- **Affected code:** `src/native/NativeVideoBridge.cpp`, `scan_task()`, `send_row()`, `epd_video_flip()`, and startup calls through `settle_level()`.
- **Trigger / reproduction:** Start a fast-video consumer such as Model Viewer, then cause the raw EPD scan task to encounter an `esp_lcd_panel_io_tx_color()` failure after a caller has queued a blocking `epd_video_flip()` during the current scan but before the next scan begins. A transient I80/LCD DMA/bus failure is sufficient.
- **Observed / logically demonstrated failure:** `epd_video_flip()` stores the calling task in `g_flip_waiter`, sets `g_flip_req`, and then waits with `ulTaskNotifyTake(..., portMAX_DELAY)`. The scan task only consumes that request and notifies the waiter at the beginning of a scan. If `send_row()` fails first, `scan_task()` sets `g_running = false` and exits directly. Its exit path never clears the queued request and never notifies `g_flip_waiter`. The caller therefore remains blocked forever and cannot reach `epd_video_shutdown()`, whose waiter-notification logic would otherwise release it.
- **Likely root cause:** The scan-task fatal-error path bypasses the same waiter cancellation/notification protocol used by explicit shutdown.
- **Impact:** A recoverable display-transport fault can become a permanent native-app/startup hang requiring an external reset; the raw display resources may also remain active because the blocked caller cannot execute normal teardown.
- **Repair direction:** Centralize scan-task termination so every fatal exit atomically clears `g_flip_req`, captures and clears `g_flip_waiter`, marks the engine stopped, and notifies the waiter before deleting the task. Consider making blocking flips return success/failure or use a bounded wait so callers can propagate transport failure. Add a fault-injection test that fails `send_row()` with a queued waiter and proves the producer wakes and teardown completes.

### 41. USB Debug redraws stale devices when the discovery snapshot fails

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0520](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0520/bugs.md)

- **Affected code:** `Apps/usb_debug.c`, `refresh_devices()` and the main event loop path that reacts to processed USB discovery events.
- **Trigger / reproduction:** Let USB Debug successfully enumerate at least one device, then make `discovery_api->devices()` fail on a later refresh—for example because enumeration temporarily fails, the device set changes during snapshotting, or the provider reports that more than `USB_DEBUG_DEVICE_LIMIT` (8) tokens are required.
- **Observed / logically demonstrated failure:** On a successful refresh, `device_count`, `devices[]`, and `rows[]` are populated. On a later `devices()` failure, `refresh_devices()` updates only `status_text` and returns `false`; it does not clear or replace the old snapshot. In the normal event-processing branch, the caller explicitly ignores that return value with `(void)refresh_devices()` and redraws. The screen can therefore continue showing and accepting selection of devices from the previous snapshot even though the current provider snapshot failed. Confirm/Inspect then uses stale device tokens.
- **Likely root cause:** Device-list refresh is not transactional and the failure path leaves the previous successful snapshot marked as current, while one caller treats refresh failure as non-fatal.
- **Impact:** Detached or no-longer-addressable USB devices can remain visible and selectable, producing misleading descriptor/control-transfer errors and potentially operating on an invalid or reused provider token instead of the currently attached set.
- **Repair direction:** Build the next device snapshot in temporary storage and publish it only on complete success; on failure, explicitly clear `device_count`/rows or mark the existing snapshot unavailable and disable Inspect. The event loop must branch on the refresh result rather than redrawing stale state. Add tests for a success followed by snapshot failure and for the provider returning a required count above 8.

### 42. Shared text wrapping drops the rest of a paragraph after an over-width first token

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0520](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0520/bugs.md)

- **Affected code:** `src/components/themes/BaseTheme.cpp`, `BaseTheme::wrappedTextForRole()`; visible callers include `src/native/NativeUiBridge.cpp::renderTextView()`.
- **Trigger / reproduction:** Render a paragraph with `maxLines > 1` whose first whitespace-delimited token is wider than `maxWidth`, followed by additional words—for example a long URL/hash/path followed by ordinary explanatory text. The native `render_text_view` API is a direct reproduction surface.
- **Observed / logically demonstrated failure:** When `currentLine` is empty and the first token exceeds `maxWidth`, `wrappedTextForRole()` appends a truncated form of that one token and immediately `return lines;`. Any remaining text in `remaining` is discarded even when many output lines are still available. `renderTextView()` then treats the shortened vector as the complete paragraph, so its reported `total_lines` also hides the lost text.
- **Likely root cause:** The over-width-token branch uses an unconditional early return that is only appropriate when the caller has actually exhausted `maxLines`.
- **Impact:** Logs, USB/serial diagnostics, URLs, hashes, file paths, book metadata, or other user content can silently lose all text following a long leading token, making diagnostic and document-like views incomplete without any truncation indicator.
- **Repair direction:** After emitting/splitting an over-width token, continue processing `remaining` whenever `lines.size() < maxLines`; return only when the line budget is exhausted. Prefer character-safe splitting of oversized UTF-8 tokens rather than discarding their tail. Add regression tests for an oversized first token followed by normal words, an oversized token after a normal line, and `maxLines == 1`.

### 43. An unconsumed System UI result blocks later keyboard and Wi-Fi requests

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0725](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0725/bugs.md)

- **Affected code:** `src/native/NativeSystemUiBridge.cpp`: `keyboardState`, `wifiState`, `hasUnreadResult()`, `keyboardRequest()`, `wifiRequest()`, the two wrapper activity `loop()` methods, and `nativeSystemUiBegin()`.
- **Trigger / reproduction:** App A requests keyboard or Wi-Fi UI and returns. After the firmware activity completes and A is relaunched, make A return before consuming the pending result. Then launch app B and have it request keyboard or Wi-Fi UI.
- **Observed / logically demonstrated failure:** The wrapper marks a global result available before relaunching A. A successful relaunch does not clear it, and normal app teardown also leaves it intact because `nativeSystemUiBegin()` resets only `navigation`. Both request functions reject while `hasUnreadResult()` is true, so one abandoned result prevents unrelated later apps from opening either shared UI.
- **Likely root cause:** Pending continuation results are global and are not bound to the requesting native-app invocation or cleaned up when that resumed invocation ends.
- **Impact:** An early-return or error path in one app can make keyboard entry and Wi-Fi selection unavailable to other apps until the stale result is consumed or the device restarts.
- **Repair direction:** Bind each pending result to a request/session generation, allow only the resumed owner to consume it, and clear an unread result when that resumed invocation ends or a different app begins. Add a cross-app lifecycle regression.

### 44. OPDS load failures expose stale servers from the previous successful load

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0725](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0725/bugs.md)

- **Affected code:** `src/OpdsServerStore.cpp::loadFromFile()`, `src/JsonSettingsIO.cpp::loadOpds()`, and `src/native/NativeOpdsBridge.cpp::countServers()` / `readServer()`.
- **Trigger / reproduction:** Successfully load one or more OPDS servers, then make `/.crosspoint/opds.json` unreadable, empty, truncated, or malformed and call the native OPDS count/read path again.
- **Observed / logically demonstrated failure:** On malformed JSON, `loadOpds()` returns before clearing the existing server vector. On an empty or failed file read, `loadFromFile()` also returns false without invalidating the prior vector. The native bridge ignores that false return and reports `getCount()` or `getServer(index)`, so callers receive the previous server data as though the reload succeeded.
- **Likely root cause:** Reload failure leaves an old in-memory snapshot marked implicitly usable, and the bridge does not propagate load failure.
- **Impact:** After storage corruption or removal, the UI can continue presenting and using obsolete OPDS endpoints and account data that are no longer backed by valid persistent state.
- **Repair direction:** Parse into temporary state and publish it only after a successful complete load, or invalidate the live cache on failure. Make native count/read calls honor load failure. Add a regression that loads valid data, injects malformed/empty/read-failed storage, and verifies no old server is returned.

### 45. Time Zone silently hides the last 48 America cities

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0824](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0824/bugs.md)

- **Affected code:** `Apps/time_zone.c`, `load_rows()`, `activate()`, and initial selected-city discovery; authoritative counts come from `src/native/NativeTimeZoneBridge.cpp` and `lib/hal/TimeZoneData.cpp`.
- **Trigger / reproduction:** Open Time Zone, choose the America region, and try to select any city whose region-relative index is 96 or greater. The generated catalog currently contains 144 `America/*` entries.
- **Observed / logically demonstrated failure:** The native bridge reports all 144 America cities, but the app defines `MAX_ROWS 96u` and clamps `row_count` to that value. The city list therefore exposes only indices 0–95; 48 valid America entries are unreachable. The initial selected-city scan is also limited by `i < MAX_ROWS`, so a currently configured city in the hidden tail is not located or highlighted. This conflicts with `TimeZoneCatalog::kMaxCitiesPerRegion == 160`.
- **Likely root cause:** The UI's fixed 96-row storage limit predates or was not reconciled with the full generated IANA catalog and has no pagination/windowing.
- **Impact:** Users cannot select 48 legitimate America time zones through the Time Zone app, and an already configured hidden-zone selection is misrepresented when the app opens.
- **Repair direction:** Remove the silent clamp by paging/windowing the authoritative city count, or allocate to the catalog's declared maximum and preserve access to all entries. The selected-city lookup must scan the complete region. Add a regression test asserting that every `city_count(region)` index is reachable, especially America indices 95, 96, and 143.

- **Consolidation sources:** [automation/bug-scan-20260928-0738](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/ae95ab57b54b4745074a22227bad25c5341ba4b2/bugs.md); [Drive: 2026-09-28 07-38 MDT - automation-bug-scan-20260928-0738 - Instructions](https://docs.google.com/spreadsheets/d/1X2Pqr2AHuLtG36KzrvkQEZ5LRxKnGR9yO4riBi6bm1c/edit?usp=drivesdk); [Drive: 2026-09-28 07-38 MDT - automation-bug-scan-20260928-0738 - Diff](https://docs.google.com/spreadsheets/d/17ntuX4WGxnYrfhLIbPor3h_0wbt-EkjvMrvPg-9TLWA/edit?usp=drivesdk)

### 46. One transient LoRa reinitialization failure after a display refresh strands the radio off for the rest of the app session

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0824](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0824/bugs.md); [2026-09-26 1921 MDT - automation-bug-scan-20260926-1921 - intended bugs.md diff](https://docs.google.com/document/d/11rg9j1Z7Jy8pNKrR3Tut8BWdTcZAvbgRYWr5MhcOzlo/edit?usp=drivesdk)

- **Affected code:** `src/native/NativeLoRaBridge.cpp`, `prepareDisplay()`, `finishDisplay()`, `initializeHardware()`; `Apps/lora.c`, `render_state()`.
- **Trigger / reproduction:** On T5S3 Pro, run LoRa and cause the SX1262 reinitialization performed after a display refresh to fail once (for example a transient SPI/radio-start failure during `finish_display()`). Then allow later UI renders after the transient condition has cleared.
- **Observed / logically demonstrated failure:** `prepareDisplay()` sets `pausedForDisplay = true` and shuts the radio down. `finishDisplay()` clears `pausedForDisplay` before calling `initializeHardware()`. If that reinitialization fails, `initializeHardware()` leaves `running = false`. On every later render, `prepareDisplay()` immediately returns true because `!running` and does not re-arm `pausedForDisplay`; `finishDisplay()` then sees `!pausedForDisplay` and returns without retrying initialization. The app ignores both display-bracketing return values, so there is no other recovery path before exit/relaunch.
- **Likely root cause:** The "paused for display" recovery intent is cleared before hardware restoration succeeds, converting a recoverable transient failure into a terminal session state.
- **Impact:** A single temporary SX1262 bring-up failure after an e-paper refresh can permanently disable receive/transmit until the user leaves and relaunches the LoRa app.
- **Repair direction:** Keep the recovery-pending state set until `initializeHardware()` succeeds, or introduce an explicit retry/error state that later `finish_display()` calls can service. Have the app surface a restoration error instead of discarding the return value. Add a bridge regression test where the first post-display initialization fails and a later render successfully retries and restores `running`/receiver state.
- **Additional symptom from the earlier handoff:** `Apps/lora.c::render_state()` renders the Ready state before attempting radio restoration and ignores its failure, so the displayed status can remain Ready after the radio has stopped.

### 47. Legacy Recent Books v2 migration loses record alignment and corrupts later entries

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0924](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0924/bugs.md)

- **Affected code:** `src/RecentBooksStore.cpp`, `RecentBooksStore::loadFromBinaryFile()`, specifically the `version == 2` migration branch.
- **Trigger / reproduction:** Boot with a genuine version-2 `/.crosspoint/recent.bin` containing two or more recent-book records. A v2 record serializes `path`, `title`, `author`, and `progress` for every book. This is especially easy to demonstrate when the first path still resolves to a readable book whose metadata can be reconstructed.
- **Observed / logically demonstrated failure:** The current v2 reader always consumes `path`, but consumes stored `title` and `author` only when live metadata lookup returns an empty title/author, and it never consumes the serialized `progress` field at all. Therefore the file cursor is left inside the first record. The next loop iteration interprets leftover title/author/progress bytes as the next path record, so later entries are corrupted, omitted, or migration fails.
- **Likely root cause:** The migration code decides whether to read serialized fields based on reconstructed metadata instead of first consuming the complete historical on-disk record shape; the legacy v2 `progress` field was also dropped without advancing past it.
- **Impact:** Upgrading from firmware that wrote v2 recent-book state can destroy the logical recent-books list during migration, with only the first entry potentially surviving correctly.
- **Repair direction:** For v2, unconditionally deserialize the full historical record (`path`, `title`, `author`, `progress`) into temporaries before deciding which title/author values to keep. Validate every read and parse into temporary state before replacing `recentBooks`. Add a fixture containing at least two v2 entries, including one whose live metadata lookup succeeds, and verify both records remain aligned after migration.

### 48. WebDAV overwrite paths delete the existing destination before the replacement is safely published

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0924](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0924/bugs.md)

- **Affected code:** `src/network/WebDAVHandler.cpp`: `WebDAVHandler::raw()` at `RAW_END` for PUT, plus overwrite handling in `handleMove()` and `handleCopy()`.
- **Trigger / reproduction:** Overwrite an existing file through WebDAV, then force the replacement publication step to fail. Examples include an SD/filesystem failure when renaming a completed `.davtmp` during PUT, a failed source rename during MOVE, or destination-create/write failure during COPY.
- **Observed / logically demonstrated failure:** PUT uploads to a temporary file, but when replacing an existing target it calls `Storage.remove(_putPath)` before attempting `tmp.rename(_putPath)`. MOVE and COPY similarly remove an existing destination before the source rename or replacement copy has succeeded. If the subsequent operation fails, the old destination has already been destroyed; PUT even contradicts its own comment that the temp file is intended to avoid destroying the original on failed upload.
- **Likely root cause:** Overwrite is implemented as destructive delete-then-publish rather than a transactional replace with rollback.
- **Impact:** A transient SD/filesystem error during an overwrite can turn a failed WebDAV operation into irreversible loss of the previously valid destination file.
- **Repair direction:** Publish replacements transactionally. Rename the old destination to a bounded backup, publish the new file, then delete the backup only after success; restore the backup if publication fails. Use the same helper for PUT/MOVE/COPY so all overwrite paths have identical rollback semantics. Add fault-injection tests at each post-backup failure point proving the original file survives.

### 49. ZIP reader rejects standards-compliant archives whose EOCD comment is longer than about 1 KB

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-0924](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-0924/bugs.md)

- **Affected code:** `lib/ZipFile/ZipFile.cpp`, `ZipFile::loadZipDetails()`.
- **Trigger / reproduction:** Open a valid ZIP whose End of Central Directory record has a comment longer than 1002 bytes. ZIP permits an EOCD comment up to 65,535 bytes, so such an archive is standards-compliant.
- **Observed / logically demonstrated failure:** `loadZipDetails()` scans only the final 1024 bytes of the file for the EOCD signature. The minimum EOCD record itself is 22 bytes, so once the comment exceeds 1002 bytes the EOCD signature lies outside that scan window and the function deterministically reports `EOCD signature not found in zip file`. Any feature using this ZIP reader then rejects the archive before central-directory parsing.
- **Likely root cause:** The implementation assumes the EOCD must be within the last 1 KB rather than honoring the ZIP format's 16-bit comment-length allowance.
- **Impact:** Valid ZIP-based content can fail to open or extract solely because it carries a legal archive comment; this can affect generic archive operations and any ZIP-backed content path using `ZipFile`.
- **Repair direction:** Search at least `22 + 65535` bytes from EOF (bounded by file size), scan backward for EOCD, and validate the candidate's comment length and central-directory bounds before accepting it. Add tests at comment lengths 0, 1002, 1003, and 65535 bytes.

### 50. MSP programmer can leak its transport/session forever when close-time target release fails

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-1124](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-1124/bugs.md)

- **Affected code:** `Drivers/program_msp/driver.c`, `close_session()`, `sync_target()`, `release_target()`, and `quiesce()`.
- **Trigger / reproduction:** Open an MSP programming session successfully, then disconnect the target/probe or otherwise make the close-time `sync_target()` or `release_target()` command fail before calling `program.msp::close()`.
- **Observed / logically demonstrated failure:** `close_session()` returns immediately when either `sync_target(s)` or `release_target(s)` fails. Those early returns occur before `msp->close(..., s->transport)` and before `*s = (program_session){0}`. The session token therefore remains allocated and its underlying MSP-FET transport is never explicitly closed. `quiesce()` subsequently returns false for any nonzero session token, so the provider can remain non-quiescent and future programming/driver shutdown can be blocked indefinitely after a target-loss error.
- **Likely root cause:** Target reset/release and host transport cleanup are coupled into one all-or-nothing success path; cleanup is skipped precisely on the error paths where it is most necessary.
- **Impact:** A cable pull or target communication failure during close can wedge `program.msp` until reboot/reload, retaining provider resources and preventing clean shutdown or later sessions.
- **Repair direction:** Make target synchronization/release best-effort during close, but always attempt `msp->close()` and retire the local session slot. Preserve/report the first close error separately. Add failure-injection tests for `sync_target()`, `release_target()`, and transport close to prove `quiesce()` eventually succeeds and no session token remains live.

### 51. Recent Books migration retires the legacy file even when the JSON save fails

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-1321](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-1321/bugs.md)

- **Affected code:** `src/RecentBooksStore.cpp`, `RecentBooksStore::loadFromFile()`, specifically the legacy `recent.bin` migration path.
- **Trigger / reproduction:** Start with a valid legacy `/.crosspoint/recent.bin` and no usable `recent.json`. Let `loadFromBinaryFile()` succeed, but force `saveToFile()` to fail (for example by exhausting writable SD space or injecting an SD write/open failure during the migration).
- **Observed / logically demonstrated failure:** After a successful binary load, `loadFromFile()` calls `saveToFile()` but ignores its return value, then unconditionally renames `recent.bin` to `recent.bin.bak`, logs that migration succeeded, and returns `true`. If the JSON write failed but the rename succeeds, the only readable persistent recent-books store has been retired. The in-memory list survives only for the current boot; on the next boot there is no valid `recent.json` and no `recent.bin` at the migration path, so the recent list disappears.
- **Likely root cause:** Unlike the state, settings, Wi-Fi, and KOReader migration paths, Recent Books does not make retirement of the legacy source conditional on successfully publishing the replacement file.
- **Impact:** A transient storage failure during one-time migration can convert a recoverable legacy recent-book history into persistent loss of that history on the next restart, while falsely reporting that migration succeeded.
- **Repair direction:** Require `saveToFile()` to succeed before renaming the legacy file. If publication fails, leave `recent.bin` in place and return/log failure so the next boot can retry. Check the rename result as well, and add a regression test that injects a JSON-save failure and verifies the legacy source remains available and migration is retried.

### 52. HalStorage::writeFile deletes the last good file before the replacement is durable

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-1321](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-1321/bugs.md)

- **Affected code:** `lib/hal/HalStorage.cpp`, `HalStorage::writeFile(const char*, const String&)` and `openFileForWriteUnlocked()`; callers include `JsonSettingsIO::saveSettings()`, `saveState()`, `saveWifi()`, `saveKOReader()`, `saveRecentBooks()`, `saveOpds()`, and `saveBookmarks()`.
- **Trigger / reproduction:** Begin with any valid JSON-backed store, then save an update while injecting an SD failure after the existing path has been removed: fail the subsequent open, produce a short write, lose power during the replacement write, or otherwise interrupt publication.
- **Observed / logically demonstrated failure:** `writeFile()` explicitly removes an existing target before opening the new file. The writer then opens with `O_TRUNC`, writes directly to the canonical pathname, and only detects a short write after closing. There is no staged file, backup, or rollback. Therefore a save that returns `false` can already have destroyed the previously valid file or left a partial replacement at its canonical path.
- **Likely root cause:** The common persistence primitive is implemented as destructive replace-in-place rather than a transactional same-directory stage-and-publish operation.
- **Impact:** A single SD write/open error or power interruption can corrupt or erase settings, state, credentials, OPDS configuration, recent books, or bookmarks that were valid before the attempted save. Callers cannot safely recover merely by observing the `false` return because the old durable value is already gone.
- **Repair direction:** Write to a unique same-directory temporary file, verify the complete byte count, sync and close it, then atomically publish it with a backup/rollback strategy that preserves the old target until the staged replacement is known good. Remove the temporary file on every failure path. Add fault-injection tests at open, partial-write, sync/close, and rename stages proving the original file survives unsuccessful saves.

### 53. Bookmark filenames collide when directory separators and underscores map to the same name

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-1321](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-1321/bugs.md)

- **Affected code:** `src/util/BookmarkUtil.cpp`, `BookmarkUtil::getBookmarkPath(const std::string& bookPath)`; consumed by `src/activities/reader/EpubReaderBookmarksActivity.cpp`.
- **Trigger / reproduction:** Put two EPUBs at paths such as `/Books/A/B.epub` and `/Books/A_B.epub`. Add bookmarks to one book, then open the bookmark UI for the other.
- **Observed / logically demonstrated failure:** `getBookmarkPath()` removes the leading slash, replaces every remaining path separator with `_`, strips the extension, and appends `.json`. Both example book paths therefore resolve to the same bookmark file: `/.crosspoint/bookmarks/Books_A_B.json`. Bookmarks saved for either book overwrite/share the other book's storage and can then be loaded as if they belonged to the current EPUB.
- **Likely root cause:** The bookmark-store key uses a lossy path sanitization instead of a collision-resistant or reversible encoding of the canonical book path.
- **Impact:** Legitimate library layouts can cross-contaminate or overwrite bookmarks between unrelated books. A loaded bookmark can carry XPath/spine metadata from the wrong EPUB, producing incorrect navigation in addition to bookmark data loss.
- **Repair direction:** Derive the bookmark filename from a collision-resistant digest of the canonical full book path (optionally retaining a readable basename prefix), or use a reversible escaping scheme that distinguishes separators from literal underscores. Provide migration/fallback for existing bookmark files and add collision tests for separators, underscores, and same basenames in different directories.

### 54. Web file operations apply hidden-item policy only to the final path component

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-1424-findings](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-1424-findings/bugs.md)

- **Affected code:** `src/network/CrossPointWebServer.cpp`, especially `handleFileListData()`, `handleDownload()`, `handleDelete()`, `HIDDEN_ITEMS`, and `isProtectedItemName()`.
- **Trigger / reproduction:** With the web file manager running, request a known ordinary filename located inside a directory that the browser intentionally hides, or request that hidden directory as the listing root.
- **Observed / logically demonstrated failure:** File listing does not reject a hidden directory supplied as its root. Download and delete authorize only the final basename, so the hidden status of a parent directory is not considered. Files below hidden/system directories are therefore handled differently from those directories in the normal browser UI.
- **Likely root cause:** The visibility/protection rule is applied to one basename rather than to the canonical path and all of its components.
- **Impact:** State/cache files intended to be excluded from web file-management operations can still be exposed to those operations when their full path is known, and destructive operations can damage device state.
- **Repair direction:** Centralize path authorization, canonicalize every user-supplied path, and reject any path containing a protected component before list/read/write/rename/move/delete operations. Apply the helper consistently to every web file-management endpoint and add nested-hidden-directory regression tests.

### 55. Font upload reports success for truncated or short-written .cpfont files

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-1424-findings](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-1424-findings/bugs.md)

- **Affected code:** `src/network/CrossPointWebServer.cpp`, `handleFontUploadData()`; `src/FontInstaller.cpp`, `FontInstaller::validateCpfontFile()`; `lib/EpdFont/SdCardFont.cpp::load()`.
- **Trigger / reproduction:** Upload a `.cpfont` that contains the expected eight-byte magic but is truncated before its complete header/TOC, or simulate an SD short write after those first bytes.
- **Observed / logically demonstrated failure:** Buffered `file.write()` return values are ignored and the upload counts requested bytes instead of bytes actually persisted. End-of-upload validation checks only the first eight magic bytes. A truncated file can therefore be reported as installed successfully even though the normal font loader later rejects it because the full header/TOC/data are missing.
- **Likely root cause:** Upload completeness and structural font validity are reduced to a magic-prefix check, and short writes do not clear the success state.
- **Impact:** Font installation can falsely report success while leaving an unusable file on SD, including an unusable replacement inside an existing family.
- **Repair direction:** Treat every short write as failure, track actual persisted length, validate the closed staged file using the full .cpfont structural rules, and only publish after complete validation. Add magic-only, truncated-header/TOC, and short-write tests.

### 56. ZIP extraction does not verify per-entry CRC-32

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-1424-findings](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-1424-findings/bugs.md)

- **Affected code:** `lib/ZipFile/ZipFile.h`, `ZipFile::FileStatSlim`; `lib/ZipFile/ZipFile.cpp`, central-directory parsing, `readFileToMemory()`, and `readFileToStream()`; consumer `src/native/NativeArchiveBridge.cpp::extract()`.
- **Trigger / reproduction:** Take a valid ZIP with a stored entry, alter one payload byte without changing the declared lengths, and extract the entry.
- **Observed / logically demonstrated failure:** Central-directory parsing skips the CRC-32 field and the cached entry metadata does not retain it. Stored extraction checks only byte count; deflated extraction checks decompression completion and final size. Neither path verifies the archive CRC, so same-length damaged data can be returned as successful extraction.
- **Likely root cause:** The lightweight ZIP metadata retained method, lengths, and offset but omitted the format's integrity field.
- **Impact:** Damaged archives can silently produce corrupted ROMs or resources while the extraction layer reports success.
- **Repair direction:** Retain CRC-32 in entry metadata, compute it incrementally over uncompressed output for both stored and deflated paths, reject mismatches before the archive bridge publishes its staged output, and add valid/corrupted archive tests.

### 57. Native atomic writes can delete unrelated sibling files ending in `.bak` or `.part`

- **Status:** Open.
- **Sources:** [automation/bug-scan-20260927-1524](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/automation/bug-scan-20260927-1524/bugs.md)

- **Affected code:** `src/native/NativePlatformBridge.cpp`, `writeFileAtomic()`, `writeStreamOpen()`, `writeStreamCommit()`, and `writeStreamAbort()`.
- **Trigger / reproduction:** Create ordinary user files `/sd/Documents/note.txt` and `/sd/Documents/note.txt.bak`, then call `write_file_atomic("/sd/Documents/note.txt", ...)`. The function sees both the destination and its computed backup name and unconditionally removes `note.txt.bak` before starting the transaction. Likewise, create `/sd/Documents/note.txt.part` and begin either an atomic write or streamed write to `note.txt`; the existing `.part` sibling is unconditionally removed.
- **Observed / logically demonstrated failure:** The transaction implementation derives staging names by appending fixed suffixes to the caller's path. At entry, an existing `destination + ".bak"` is deleted whenever the destination also exists, and an existing `destination + ".part"` is removed without any proof that either file was created by an interrupted RiscRTE transaction. The public storage API places no reservation on those filenames, so legitimate files in the same directory can be destroyed simply by writing another file whose base name collides with them.
- **Likely root cause:** Recovery state is inferred only from predictable sibling filenames rather than from transaction ownership metadata or an isolated staging namespace.
- **Impact:** Saving one document can permanently delete a different valid user file. The same collision exists for streamed writes, so apps using the newer transaction API can cause the loss without ever touching the sibling explicitly.
- **Repair direction:** Keep staging/backup objects in a firmware-owned transaction directory or use collision-resistant internal names plus a validated transaction journal. Recovery should remove or restore only files proven to belong to an interrupted transaction; never treat arbitrary user-visible `.bak` or `.part` siblings as disposable. Add tests with pre-existing suffix-collision files and verify they remain byte-for-byte intact through success, failure, abort, and recovery.

### 58. OPDS failed saves are not transactional

- **Status:** Open.
- **Sources:** [2026-09-26 1730 MDT - automation-bug-scan-20260926-1730 - intended bugs.md diff](https://docs.google.com/document/d/1IDPXJJHEy-jfbQkZn4JN1M90w-GguwxKdvS7j-c2ynI/edit?usp=drivesdk)

Affected code: src/OpdsServerStore.cpp (addServer(), updateServer(), removeServer(), saveToFile()), src/JsonSettingsIO.cpp::saveOpds(), lib/hal/HalStorage.cpp::writeFile(), and src/native/NativeOpdsBridge.cpp::countServers()/readServer().
Trigger / reproduction: Configure an OPDS server, then add, edit, or delete one while inducing an SD open or short-write failure.
Observed / logically demonstrated failure: The store mutates its live servers vector before saving and does not restore it when saving fails. The backing write removes the old JSON before creating its replacement, so a failed replacement can also eliminate the last good /.crosspoint/opds.json. Native bridge reads call loadFromFile() but ignore failure and can continue exposing the already-mutated RAM state.
Likely root cause: RAM state is committed before durable storage, the JSON replacement is non-atomic, and reload failures are ignored.
Impact: A failed edit can both lose the previous persisted server list and leave a change active even though the UI reported that saving failed.
Repair direction: Persist a candidate list through a synced temporary file plus atomic rename, commit the live vector only after success, roll back on failure, and propagate reload failure. Add injected open/short-write regression tests.

- **Consolidation sources:** [automation/bug-scan-20260929-0427](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/dc6897e74b7c37d55d43e6b711951cb7cc8d25e3/bugs.md); [Drive: 2026-09-29 0427 - automation-bug-scan-20260929-0427 - Instructions](https://docs.google.com/spreadsheets/d/1t59zkOqhm-R5DDAkGzYzXZV7OYMycN2a7VESAjxX4LE/edit?usp=drivesdk); [Drive: 2026-09-29 0427 - automation-bug-scan-20260929-0427 - Diff](https://docs.google.com/spreadsheets/d/1UGD9EGzpw3_BHk-0Timu5HoPY_Hvdkn-5pbCv3jVUF8/edit?usp=drivesdk)

### 59. File Browser silently truncates directories at 256 entries

- **Status:** Open.
- **Sources:** [2026-09-26 1730 MDT - automation-bug-scan-20260926-1730 - intended bugs.md diff](https://docs.google.com/document/d/1IDPXJJHEy-jfbQkZn4JN1M90w-GguwxKdvS7j-c2ynI/edit?usp=drivesdk); [2026-09-27 12-21 MDT - automation-bug-scan-20260927-1221 - intended bugs.md diff](https://docs.google.com/spreadsheets/d/17BY2jMeFoI2yFrSMEQc9FuTKEK3QlwSZoJfKFKzs_f0/edit?usp=drivesdk)

Affected code: Apps/file_browser.c, MAX_ENTRIES, load_sd_files(), load_usb_files(), and SD-root USB entry insertion.
Trigger / reproduction: Open an SD or USB directory containing more than 256 visible entries. Also test an SD root with 256 entries while USB storage is mounted.
Observed / logically demonstrated failure: Both loaders stop when entry_count == MAX_ENTRIES. There is no continuation cursor or truncation indicator, so later entries are absent and unreachable. At SD root the synthetic USB Storage row is only appended when capacity remains, so exactly 256 SD entries hide an attached USB volume.
Likely root cause: A fixed RAM array size doubles as the total enumeration limit instead of only the rendered/page window.
Impact: Large folders are only partially accessible, and USB storage can disappear from File Browser despite being mounted.
Repair direction: Use bounded page/window storage with offset/cursor-backed rescans, keep synthetic roots independent of file-entry capacity, and show an explicit limit indication if a hard cap remains. Add tests with 257+ entries and 256 SD-root entries plus mounted USB.

### 60. Font Family hides installed families after the first 62 SD-card choices

- **Status:** Open.
- **Sources:** [2026-09-26 1730 MDT - automation-bug-scan-20260926-1730 - intended bugs.md diff](https://docs.google.com/document/d/1IDPXJJHEy-jfbQkZn4JN1M90w-GguwxKdvS7j-c2ynI/edit?usp=drivesdk)

Affected code: Apps/font_selection.c, MAX_CHOICES and load_choices(); src/native/NativeFontBridge.cpp::choiceCount(); lib/EpdFont/SdCardFontRegistry.h, MAX_SD_FAMILIES.
Trigger / reproduction: Install more than 62 SD-card font families, then open Font Family. The registry supports up to 128 SD families and the Font Family list prepends two built-in choices.
Observed / logically demonstrated failure: NativeFontBridge::choiceCount() reports the two built-ins plus every discovered SD family, but font_selection.c clamps that total to MAX_CHOICES == 64. Therefore only 62 SD families can be loaded or rendered. If the active family lies beyond that prefix, its selected flag is never read and selected_index remains at row 0 even though row 0 is not the active font.
Likely root cause: The UI fixed choice-array capacity is smaller than the supported registry capacity and there is no paging/windowing layer.
Impact: Installed fonts can be impossible to select or see, and the highlighted row can misrepresent the actual setting.
Repair direction: Size the choice model for the supported registry count plus built-ins or page/virtualize the list without truncating the service count. Preserve selected-family lookup across the full catalog. Add regression coverage with 63+ SD families and with the active family beyond the first 62.

### 61. Springboard loses canonical package identity when package ID differs from ELF basename

- **Status:** Open.
- **Sources:** [2026-09-26 1823 MDT - automation-bug-scan-20260926-1823 - intended bugs.md diff](https://docs.google.com/document/d/1rHExVwvDfo17_559sJVPZweL6bsbxa4MYGGoJgVwe_A/edit?usp=drivesdk)

- Affected code: src/native/NativeAppHost.cpp, installedRefresh() and requestLaunch(); compare src/native/InstalledAppPath.cpp::resolveInstalledAppPath().
- Reproduction: install a valid canonical app package with ID notes-pro and artifact editor.elf, refresh Springboard, and launch it.
- Failure: installedRefresh() discovers the canonical directory by its package ID but stores only t5_app_manifest_t. requestLaunch() later derives an ID by stripping .elf from file_name. It therefore searches for package editor and, if that does not exist, falls back to /sd/Apps/editor.elf instead of /sd/Apps/notes-pro/editor.elf. If another canonical package named editor exists with the same artifact basename, selection can resolve to that package instead.
- Root cause: the installed-app cache discards the verified canonical package ID/path and later reconstructs identity from the artifact basename even though package ID and artifact basename are not required to match.
- Impact: a valid app can appear in Springboard but fail to launch; basename collisions can resolve the selected row to a different canonical package.
- Repair: store the verified package ID/exact launch path with each installed manifest and have requestLaunch() use that stored identity. Keep legacy flat-file entries explicitly tagged. Add tests for ID != artifact and duplicate artifact basenames.

### 62. Package Manager exposes only 64 rows even though the firmware inventory supports 128 installed packages

- **Status:** Open.
- **Sources:** [2026-09-26 1823 MDT - automation-bug-scan-20260926-1823 - intended bugs.md diff](https://docs.google.com/document/d/1rHExVwvDfo17_559sJVPZweL6bsbxa4MYGGoJgVwe_A/edit?usp=drivesdk); [2026-09-27 06-24 MDT - automation_bug-scan-20260927-0624 - Diff](https://docs.google.com/spreadsheets/d/1P6vUhvLdn-V4g8YjZEoDVYbpd6JtgH2N64eEO5RxnSY/edit?usp=drivesdk)

- Affected code: Apps/package_manager.c, MAX_ITEMS and refresh(); src/native/NativePackageManagerBridge.cpp, kMaxInstalledPackages, refreshInstalled(), and installedCount().
- Reproduction: install more than 64 managed packages across Apps/Drivers/Services/Providers, or install exactly 64 managed packages and place another valid package under /sd/Packages/Inbox. Open Package Manager.
- Failure: the native bridge inventories up to 128 installed packages, but the ELF allocates 64 rows and stops adding installed rows at MAX_ITEMS. When those 64 slots are full, the inbox scan still reads entries but discards all of them because row_count >= MAX_ITEMS. There is no pagination or truncation notice.
- Root cause: installed inventory and inbox are merged into a single fixed 64-entry UI buffer whose capacity is lower than the backend installed-package contract.
- Impact: packages beyond row 64 cannot be inspected, updated, or uninstalled in Package Manager; a full installed list can hide the entire inbox, and fixed root ordering can systematically hide later package kinds.
- Repair: paginate or virtualize the complete installed inventory and keep inbox access independent. At minimum expose truncation explicitly. Add tests for 65 and 128 installed packages plus a full installed page with an inbox-only package.

### 63. Time Card accepts malformed manual times and silently stores a different time

- **Status:** Open.
- **Sources:** [2026-09-26 1823 MDT - automation-bug-scan-20260926-1823 - intended bugs.md diff](https://docs.google.com/document/d/1rHExVwvDfo17_559sJVPZweL6bsbxa4MYGGoJgVwe_A/edit?usp=drivesdk)

- Affected code: Apps/timecard.c parse_time(), with persistence through consume_keyboard() and set_punch().
- Reproduction: edit a punch and enter 9:123, 09:30xyz, or 9am pm, then confirm.
- Failure: after a colon, parse_time() consumes at most two minute digits and scans the rest only for a/A or p/P. It never requires clean end-of-input. Therefore 9:123 becomes 9:12, 09:30xyz becomes 09:30, and 9am pm sets both meridiem flags with PM winning, producing 21:00. The result is then persisted.
- Root cause: permissive character scanning instead of complete-input validation, with no mutual exclusion for AM/PM.
- Impact: a typo can silently become a different valid punch and alter time-card totals.
- Repair: accept only documented complete formats, trim whitespace, validate minute digits, allow at most one meridiem suffix, and require complete input consumption. Add rejection tests for extra digits, trailing junk, and mixed AM/PM.

- **Consolidation sources:** [automation/bug-scan-20260929-0427](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/dc6897e74b7c37d55d43e6b711951cb7cc8d25e3/bugs.md); [Drive: 2026-09-29 0427 - automation-bug-scan-20260929-0427 - Instructions](https://docs.google.com/spreadsheets/d/1t59zkOqhm-R5DDAkGzYzXZV7OYMycN2a7VESAjxX4LE/edit?usp=drivesdk); [Drive: 2026-09-29 0427 - automation-bug-scan-20260929-0427 - Diff](https://docs.google.com/spreadsheets/d/1UGD9EGzpw3_BHk-0Timu5HoPY_Hvdkn-5pbCv3jVUF8/edit?usp=drivesdk)

### 64. OPDS parser cleanup dereferences a null parser after malformed XML

- **Status:** Open.
- **Sources:** [2026-09-27 10-22 MDT - automation-bug-scan-20260927-1022 - intended bugs.md diff](https://docs.google.com/spreadsheets/d/1RJR4t4F0OWFhUKHAPQJAJdPi7t7XIVy0MOur3t1_lmY/edit?usp=drivesdk)

- **Affected code:** `lib/OpdsParser/OpdsParser.cpp`, `OpdsParser::write()` and `OpdsParser::flush()`; `lib/OpdsParser/OpdsStream.cpp`, `OpdsParserStream::~OpdsParserStream()`; caller `src/activities/browser/OpdsBookBrowserActivity.cpp::fetchFeed()`.
- **Trigger / reproduction:** Browse an OPDS server whose response contains a syntax error in the XML before the end of the feed.
- **Observed / logically demonstrated failure:** When Expat reports a parse error, `OpdsParser::write()` sets `errorOccured` and calls `destroyXmlParser(parser)`, which frees the parser and sets it to `nullptr`. When `OpdsParserStream` later leaves scope, its destructor unconditionally calls `parser.flush()`. `OpdsParser::flush()` then unconditionally calls `XML_Parse()` using the null parser pointer. Instead of reaching the existing parse-error UI, cleanup can fault.
- **Likely root cause:** `flush()` assumes the Expat object is still alive even though the error path already destroys it.
- **Impact:** A malformed or truncated OPDS response can terminate the browsing flow instead of producing a recoverable parse error.
- **Repair direction:** Make `flush()` return immediately when `errorOccured` is already set or `parser == nullptr`; only finalize a live parser. Add a regression test that feeds malformed XML, destroys `OpdsParserStream`, and verifies a graceful error result.

### 65. EPUB `./` path segments are retained and break ZIP entry lookup

- **Status:** Open.
- **Sources:** [2026-09-27 10-22 MDT - automation-bug-scan-20260927-1022 - intended bugs.md diff](https://docs.google.com/spreadsheets/d/1RJR4t4F0OWFhUKHAPQJAJdPi7t7XIVy0MOur3t1_lmY/edit?usp=drivesdk)

- **Affected code:** `lib/FsHelpers/FsHelpers.cpp::normalisePath()`; consumers include `lib/Epub/Epub/parsers/ContentOpfParser.cpp` and `lib/Epub/Epub.cpp::readItemContentsToBytes()`, `readItemContentsToStream()`, and `getItemSize()`.
- **Trigger / reproduction:** Open an EPUB whose OPF is under `OPS/` and contains `href="./chapter.xhtml"`, with the corresponding ZIP entry stored as `OPS/chapter.xhtml`.
- **Observed / logically demonstrated failure:** `normalisePath()` handles `..` but treats `.` as an ordinary path component. It converts `OPS/./chapter.xhtml` to the same non-canonical string instead of `OPS/chapter.xhtml`. `ContentOpfParser` stores that value and later `ZipFile` lookup searches for the non-existent `OPS/./chapter.xhtml` entry, so the referenced content cannot be opened.
- **Likely root cause:** Current-directory segments are omitted from the normalizer's special cases.
- **Impact:** EPUBs that use explicit `./` relative references can fail to load chapters, navigation documents, CSS, covers, or other manifest resources that are present in the archive.
- **Repair direction:** Ignore `.` components during normalization while preserving the existing `..` handling. Add unit cases for `OPS/./chapter.xhtml`, `./chapter.xhtml`, repeated slashes, `a/b/../c`, and trailing `.`, plus an EPUB fixture using `./` manifest references.

### 66. EPUB guide `start` fallback condition is inverted

- **Status:** Open.
- **Sources:** [2026-09-27 10-22 MDT - automation-bug-scan-20260927-1022 - intended bugs.md diff](https://docs.google.com/spreadsheets/d/1RJR4t4F0OWFhUKHAPQJAJdPi7t7XIVy0MOur3t1_lmY/edit?usp=drivesdk)

- **Affected code:** `lib/Epub/Epub/parsers/ContentOpfParser.cpp`, guide `<reference>` handling in `startElement()`.
- **Trigger / reproduction:** Open an EPUB 2 book whose guide has a `type="start"` reference but no `type="text"` reference. Also test a guide containing `text` followed by `start`.
- **Observed / logically demonstrated failure:** The assignment condition is `type == "text" || (type == "start" && !self->textReferenceHref.empty())`. A start-only guide is ignored because the field is empty. If a text reference has already populated the field, a later start reference passes the non-empty test and overwrites it. The fallback is therefore accepted in the opposite state from the intended behavior.
- **Likely root cause:** The emptiness test for the `start` fallback is inverted.
- **Impact:** Some EPUB 2 books open at the wrong initial content location, and guide ordering can change which declared start target wins.
- **Repair direction:** Keep `text` authoritative and accept `start` only while no target has been selected, e.g. `type == "text" || (type == "start" && self->textReferenceHref.empty())`. Add tests for start-only, text-only, start-before-text, and text-before-start guides.

### 67. OPDS relative links are resolved as children of the feed filename

- **Status:** Open.
- **Sources:** [2026-09-27 06-24 MDT - automation_bug-scan-20260927-0624 - Diff](https://docs.google.com/spreadsheets/d/1P6vUhvLdn-V4g8YjZEoDVYbpd6JtgH2N64eEO5RxnSY/edit?usp=drivesdk)

- **Affected:** src/util/UrlUtils.cpp: UrlUtils::buildUrl(); src/activities/browser/OpdsBookBrowserActivity.cpp: navigateToEntry(), downloadBook()
- **Trigger:** Use a feed URL such as https://example.test/opds/root.xml containing href="books.xml" or a relative EPUB acquisition link, then open or download it.
- **Failure:** buildUrl() appends '/' plus the relative path when the base lacks a trailing slash, producing https://example.test/opds/root.xml/books.xml instead of https://example.test/opds/books.xml.
- **Root cause / impact:** The helper treats every non-slash-terminated base as a directory instead of resolving against the base document's containing directory. Valid OPDS navigation and downloads can fail.
- **Repair:** Implement normal RFC-style relative-reference resolution, including containing-directory behavior, root-relative references, dot segments, query/fragment handling, and absolute URLs. Add focused URL-resolution tests.

### 68. Failed OTA startup can leave Wi-Fi power saving disabled

- **Status:** Open.
- **Sources:** [2026-09-27 06-24 MDT - automation_bug-scan-20260927-0624 - Diff](https://docs.google.com/spreadsheets/d/1P6vUhvLdn-V4g8YjZEoDVYbpd6JtgH2N64eEO5RxnSY/edit?usp=drivesdk)

- **Affected:** src/network/OtaUpdater.cpp: OtaUpdater::installUpdate()
- **Trigger:** Enter firmware install with a newer update available and force esp_https_ota_begin() to fail, such as an allocation/start or TLS setup failure.
- **Failure:** installUpdate() sets WIFI_PS_NONE before esp_https_ota_begin(). The begin-error return happens before the later WIFI_PS_MIN_MODEM call, so Wi-Fi remains in no-power-save mode after the failed attempt.
- **Root cause / impact:** Power-policy cleanup is only after the perform loop, so the early begin failure bypasses it. A failed update can materially increase battery drain for the rest of the session.
- **Repair:** Capture the previous Wi-Fi power-save mode and restore that exact mode on every exit path using a scope guard/RAII cleanup. Add a fault-injection regression for begin failure.

### 69. Clock synchronization can suppress retry after a hardware-clock write failure.

- **Status:** Open.
- **Sources:** [2026-09-27 12-21 MDT - automation-bug-scan-20260927-1221 - intended bugs.md diff](https://docs.google.com/spreadsheets/d/17BY2jMeFoI2yFrSMEQc9FuTKEK3QlwSZoJfKFKzs_f0/edit?usp=drivesdk)

- Affected: src/ClockSync.cpp, commitCurrentSystemTime(), shouldSync().
- Reproduce: acquire valid network time, force one hardware-clock write-back failure, then call ordinary sync again within 12 hours.
- Failure: recent-sync state is set before write-back; after failure, the next ordinary sync is throttled and can report success without retrying the hardware-clock update.
- Cause: retry-throttle state is published before the full clock commit succeeds.
- Impact: persisted time can remain stale while automatic retry is suppressed for up to 12 hours.
- Repair: publish recent-sync state only after write-back succeeds, or track acquisition and persistence independently.

### 70. Automatic network reconnect does not fall back after the preferred stored network fails.

- **Status:** Open.
- **Sources:** [2026-09-27 12-21 MDT - automation-bug-scan-20260927-1221 - intended bugs.md diff](https://docs.google.com/spreadsheets/d/17BY2jMeFoI2yFrSMEQc9FuTKEK3QlwSZoJfKFKzs_f0/edit?usp=drivesdk)

- Affected: src/runtime/network/SavedNetworkConnection.cpp, ensureSavedConnection(); src/native/NativeOtaBridge.cpp, ensureOtaNetworkReady().
- Reproduce 15: configure two network profiles, make the unavailable profile preferred by recent use, then run automatic networking where the alternate profile is available.
- Failure 15: the helper attempts only the preferred profile. The alternate is selected only when the preferred record is absent, not when its connection attempt fails.
- Cause: network selection is a one-time choice instead of an ordered fallback sequence.
- Impact: automatic networking and firmware update can fail even though another stored network is reachable.
- Repair: try an ordered de-duplicated list of stored networks within one total timeout and reuse that shared policy from the update bridge.

### 71. KOReader authentication disconnects a Wi-Fi connection that was already active

- **Status:** Open.
- **Sources:** [2026-09-26 1921 MDT - automation-bug-scan-20260926-1921 - intended bugs.md diff](https://docs.google.com/document/d/11rg9j1Z7Jy8pNKrR3Tut8BWdTcZAvbgRYWr5MhcOzlo/edit?usp=drivesdk)

- **Affected code:** `src/native/NativeKOReaderBridge.cpp::endAuthSession()`; authentication cleanup in `Apps/koreader_sync.c`.
- **Trigger / reproduction:** Connect to Wi-Fi before opening KOReader Sync, perform authentication, then leave its authentication flow.
- **Failure:** `endAuthSession()` unconditionally calls `WiFi.disconnect(false)` and `WiFi.mode(WIFI_OFF)`, including when the connection existed before authentication.
- **Root cause:** Cleanup does not track ownership of the pre-existing shared network connection.
- **Impact:** Authentication disrupts connectivity established by another workflow.
- **Repair direction:** Preserve pre-existing connectivity; release only a connection owned by the authentication session. Test both preconnected and authentication-owned sessions.

### 72. Firmware Update displays the normal no-update result as an update-check failure

- **Status:** Open.
- **Sources:** [2026-09-26 1921 MDT - automation-bug-scan-20260926-1921 - intended bugs.md diff](https://docs.google.com/document/d/11rg9j1Z7Jy8pNKrR3Tut8BWdTcZAvbgRYWr5MhcOzlo/edit?usp=drivesdk)

- **Affected code:** `Apps/ota_update.c::app_main()` and `src/native/NativeOtaBridge.cpp::mapResult()`.
- **Trigger / reproduction:** Have the update service return the bridge's `T5_OTA_NO_UPDATE` result during an update check.
- **Failure:** The app treats every result other than `T5_OTA_OK` as an error before reaching its existing Up to date screen.
- **Root cause:** The explicit no-update outcome is not included in the successful/no-change state handling.
- **Impact:** Users see an update-check error for a normal no-update outcome.
- **Repair direction:** Route `T5_OTA_NO_UPDATE` to the Up to date state and keep genuine transport/metadata failures distinct. Test OK/newer, OK/current, NO_UPDATE, and error results.

### 73. Malformed indexed PNG bit depth can divide by zero during EPUB cover conversion

- **Status:** Open.

- **Affected code:** `lib/PngToBmpConverter/PngToBmpConverter.cpp`, `PngToBmpConverter::pngFileToBmpStreamInternal()` and `convertScanlineToGray()`; reachable through `lib/Epub/Epub.cpp::generateCoverBmp()` and `generateThumbBmp()`.
- **Trigger / reproduction:** Put a PNG cover in an EPUB with a syntactically readable IHDR that declares indexed color (`colorType == 3`) with an illegal bit depth greater than 8, for example 16, and provide enough PLTE/IDAT data for the converter to decode the first scanline. Open the book or let Home generate its cover thumbnail.
- **Observed / logically demonstrated failure:** The converter validates dimensions, compression, filter method, and interlace, but never validates the PNG specification's legal bit-depth/color-type combinations. For indexed color it accepts the declared depth and later computes `ppb = 8 / ctx.bitDepth`. With `bitDepth == 16`, `ppb` is zero, after which `x % ppb` and `x / ppb` in `convertScanlineToGray()` perform integer division by zero instead of rejecting the malformed image.
- **Likely root cause:** Header validation checks generic IHDR fields but omits the color-type-specific bit-depth constraints that later pixel-unpacking arithmetic assumes.
- **Impact:** A malformed or corrupted PNG embedded as an EPUB cover can fault/reboot the reader during cover or thumbnail generation rather than producing a recoverable image-decode failure.
- **Repair direction:** Reject invalid IHDR combinations before calculating row layout: grayscale 1/2/4/8/16, truecolor 8/16, indexed 1/2/4/8, grayscale+alpha 8/16, and RGBA 8/16. For indexed images also require a valid PLTE before decoding. Add malformed indexed-PNG fixtures at bit depths 16 and other unsupported values and verify conversion returns `false` without entering pixel unpacking.

- **Consolidation sources:** [automation/bug-scan-20260927-1624](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/db62793d1f95a7a9fc1046f14dc517f4d2d0ebb0/bugs.md); [automation/bug-scan-20260929-1227](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/6e24332a2bf14ad272bb4b8d7944e5e9a9cd1e1c/bugs.md); [automation/bug-scan-20260930-0247-final](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/65f8b5d9fb1195953a68ccec63882f5533f5a07f/bugs.md); [Drive: 2026-09-27 16-24 MDT - automation-bug-scan-20260927-1624 - Diff](https://docs.google.com/spreadsheets/d/1MJXMqUjsh2w_A9g6xCho9apGyQELbox0583ao0NCPVE/edit?usp=drivesdk); [Drive: 2026-09-27 16-24 MDT - automation-bug-scan-20260927-1624 - Instructions](https://docs.google.com/spreadsheets/d/1Z8JO7Ihn4rBSo7o8DoqTWoDVtXQRGaVYIsifuACn_Z0/edit?usp=drivesdk); [Drive: 2026-09-30 02-47 MDT - automation-bug-scan-20260930-0247-final - Diff](https://docs.google.com/spreadsheets/d/1oeF8rknOMfAoXC_W8xChkdJbdheYV1kaZNt4k9dyrsM/edit?usp=drivesdk); [Drive: 2026-09-29 1227 MDT - automation-bug-scan-20260929-1227 - Instructions](https://docs.google.com/document/d/1W1g8tZYLjNvZt4NIm61ESqN6j60PCManQaqLmS_7yj0/edit?usp=drivesdk); [Drive: 2026-09-29 1227 MDT - automation-bug-scan-20260929-1227 - Diff](https://docs.google.com/document/d/1fZ8vZ0C1ZjmKnHULiYYQSDc1Cg7AqNSix4w-kpxSVwY/edit?usp=drivesdk); [Drive: 2026-09-30 02-47 MDT - automation-bug-scan-20260930-0247-final - Instructions](https://docs.google.com/spreadsheets/d/1ASSpLAik3zeGb_FhhyutJXUKiqxVgAixSFrSd6DZq5E/edit?usp=drivesdk)

### 74. KOReader binary document hashing silently succeeds after partial SD reads

- **Status:** Open.

- **Affected code:** `lib/KOReaderSync/KOReaderDocumentId.cpp`, `KOReaderDocumentId::calculate()`; caller `src/activities/reader/KOReaderSyncActivity.cpp::performSync()`.
- **Trigger / reproduction:** Select KOReader's binary document-match method, then inject an SD seek failure at one sampled offset or make a sampled `file.read()` return fewer bytes than the requested `bytesToRead` while hashing an otherwise readable EPUB.
- **Observed / logically demonstrated failure:** A failed `seekSet()` is logged and skipped with `continue`. A short read is accepted whenever it returns more than zero bytes, and only those bytes are added to the MD5. After the loop the function always finalizes and returns a non-empty digest; it never requires every in-range sample to have been read completely. `KOReaderSyncActivity` treats any non-empty digest as valid and uses it for remote progress lookup/upload.
- **Likely root cause:** The binary matcher treats storage I/O as best-effort sampling even though the digest is a persistent document identity and must be deterministic for a given file.
- **Impact:** A transient SD error can produce a different but apparently valid document ID, making existing KOReader progress appear missing and allowing progress to be uploaded under a phantom document key. Later healthy reads calculate the normal ID again, splitting sync history across hashes.
- **Repair direction:** Treat any required seek failure or short read as a hash failure and return an empty result so the UI reports `STR_HASH_FAILED`. Require `bytesRead == bytesToRead` for each in-range sample and add failure-injection tests for seek failure, zero-byte read, and positive short read proving no digest is emitted.

- **Consolidation sources:** [automation/bug-scan-20260927-1624](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/db62793d1f95a7a9fc1046f14dc517f4d2d0ebb0/bugs.md); [automation/bug-scan-20260928-1020](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/a6c2e0dcd3345f9dd7946e444b7b8263e921d201/bugs.md); [automation/bug-scan-20260929-1126](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b99d1c80c4201e399366f9d7d7bf685655a79655/bugs.md); [automation/bug-scan-20260930-0247-final](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/65f8b5d9fb1195953a68ccec63882f5533f5a07f/bugs.md); [Drive: 2026-09-28 10-20 MDT - automation-bug-scan-20260928-1020 - Instructions](https://docs.google.com/spreadsheets/d/1mLlBAkKzZu2O97mGoH_rs1TACJzlhD7wx1Xs5oY6EIY/edit?usp=drivesdk); [Drive: 2026-09-27 16-24 MDT - automation-bug-scan-20260927-1624 - Diff](https://docs.google.com/spreadsheets/d/1MJXMqUjsh2w_A9g6xCho9apGyQELbox0583ao0NCPVE/edit?usp=drivesdk); [Drive: 2026-09-27 16-24 MDT - automation-bug-scan-20260927-1624 - Instructions](https://docs.google.com/spreadsheets/d/1Z8JO7Ihn4rBSo7o8DoqTWoDVtXQRGaVYIsifuACn_Z0/edit?usp=drivesdk); [Drive: 2026-09-30 02-47 MDT - automation-bug-scan-20260930-0247-final - Diff](https://docs.google.com/spreadsheets/d/1oeF8rknOMfAoXC_W8xChkdJbdheYV1kaZNt4k9dyrsM/edit?usp=drivesdk); [Drive: 2026-09-29 1126 MDT - automation-bug-scan-20260929-1126 - Instructions](https://docs.google.com/document/d/10MhueVepOY43NRSwVh6FkJecKy6xzQreFVj9GEyzah0/edit?usp=drivesdk); [Drive: 2026-09-29 1126 MDT - automation-bug-scan-20260929-1126 - Diff](https://docs.google.com/spreadsheets/d/1tileMLV2UkvBPsJcKLms1yet2ggp9mwPW2CYX4BcQgo/edit?usp=drivesdk); [Drive: 2026-09-28 10-20 MDT - automation-bug-scan-20260928-1020 - Diff](https://docs.google.com/spreadsheets/d/1JXPswBeGnJhUCpvWcsZ020laGpgrq7qhh5Woc7M95MY/edit?usp=drivesdk); [Drive: 2026-09-30 02-47 MDT - automation-bug-scan-20260930-0247-final - Instructions](https://docs.google.com/spreadsheets/d/1ASSpLAik3zeGb_FhhyutJXUKiqxVgAixSFrSd6DZq5E/edit?usp=drivesdk)

### 75. Wi-Fi credential changes remain live when persistence fails and the UI proceeds as if they were saved

- **Status:** Open.

- **Affected code:** `src/WifiCredentialStore.cpp`, `WifiCredentialStore::addCredential()`, `removeCredential()`, `setLastConnectedSsid()`, `clearLastConnectedSsid()`, and `clearAll()`; `src/activities/network/WifiSelectionActivity.cpp`, save/forget/connected-state handling.
- **Trigger / reproduction:** Connect to a Wi-Fi network and choose to save its password while forcing the settings SD write (`JsonSettingsIO::saveWifi()` / `Storage.writeFile()`) to fail. The same problem can be exercised while forgetting a saved network. Continue using the Wi-Fi UI, then either reboot or cause a later credential-store save to succeed.
- **Observed / logically demonstrated failure:** `addCredential()` changes the existing credential or pushes a new one before calling `saveToFile()`. On failure it returns `false` but leaves that mutation in the live `credentials` vector. `WifiSelectionActivity` ignores the return value and completes the save workflow as though it succeeded. The remove path similarly erases the live credential before persistence and the UI updates its saved-password state regardless of the returned result. The void last-connected/clear helpers also discard save failures. After a failed write, runtime state therefore disagrees with disk: reboot can resurrect an allegedly forgotten network or lose an allegedly saved password, while a later successful save can unexpectedly make the earlier failed mutation durable.
- **Likely root cause:** The credential store mutates its authoritative in-memory state before durable persistence and several callers discard the persistence result.
- **Impact:** Wi-Fi credentials and preferred-network state can be silently lost, resurrected, or persisted later despite the user having been told the preceding operation completed.
- **Repair direction:** Make credential mutations transactional: construct a candidate store, persist it, and publish it to live state only after the write succeeds (or explicitly roll back on failure). Return persistence status from the last-connected/clear helpers and have the Wi-Fi UI keep the prompt/error state visible when saving or forgetting fails. Add failure-injection tests for add/update/remove/preferred-network writes followed by reboot and by a later successful save.

- **Consolidation sources:** [automation/bug-scan-20260927-1624](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/db62793d1f95a7a9fc1046f14dc517f4d2d0ebb0/bugs.md); [automation/bug-scan-20260928-0738](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/ae95ab57b54b4745074a22227bad25c5341ba4b2/bugs.md); [automation/bug-scan-20260929-0725](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/7c3008d0876ab9bb0a728aca781b63fbfa9613f3/bugs.md); [Drive: 2026-09-27 16-24 MDT - automation-bug-scan-20260927-1624 - Diff](https://docs.google.com/spreadsheets/d/1MJXMqUjsh2w_A9g6xCho9apGyQELbox0583ao0NCPVE/edit?usp=drivesdk); [Drive: 2026-09-27 16-24 MDT - automation-bug-scan-20260927-1624 - Instructions](https://docs.google.com/spreadsheets/d/1Z8JO7Ihn4rBSo7o8DoqTWoDVtXQRGaVYIsifuACn_Z0/edit?usp=drivesdk); [Drive: 2026-09-29 0725 MDT - automation-bug-scan-20260929-0725 - instructions](https://docs.google.com/spreadsheets/d/1-4jDQiaUOBHv4-fTEqOZCIfyr8g9jYm1HMFC0fqAc_o/edit?usp=drivesdk); [Drive: 2026-09-28 07-38 MDT - automation-bug-scan-20260928-0738 - Instructions](https://docs.google.com/spreadsheets/d/1X2Pqr2AHuLtG36KzrvkQEZ5LRxKnGR9yO4riBi6bm1c/edit?usp=drivesdk); [Drive: 2026-09-28 07-38 MDT - automation-bug-scan-20260928-0738 - Diff](https://docs.google.com/spreadsheets/d/17ntuX4WGxnYrfhLIbPor3h_0wbt-EkjvMrvPg-9TLWA/edit?usp=drivesdk); [Drive: 2026-09-29 0725 MDT - automation-bug-scan-20260929-0725 - diff](https://docs.google.com/spreadsheets/d/1AJPUdv9PVLhPLWMG1wuU6fzACHMVp59JWvFPEDsjuQ0/edit?usp=drivesdk)

### 76. Font catalog refresh leaves a usable partial catalog after rejecting the manifest

- **Status:** Open.

- **Affected code:** `src/native/NativeFontBridge.cpp::refreshCatalog()`, `familyCount()`, and `installFamily()`; `Apps/font_manager.c::app_main()`, `load_rows()`, and `activate_selected()`.
- **Trigger / reproduction:** Serve a font manifest whose first family is valid and whose later family contains an invalid family name, invalid `.cpfont` filename, or missing/invalid `crc32`. Open **Manage Fonts** and let the catalog refresh fail.
- **Observed / logically demonstrated failure:** `refreshCatalog()` clears the process-global `families` vector, then appends each family as it is parsed. If a later entry fails validation, the function returns `T5_FONT_MANIFEST_ERROR` without clearing or rolling back the families already appended. Font Manager records the **Invalid font catalog** status but immediately calls `render()`; `load_rows()` still obtains the nonzero partial `family_count()`, and `activate_selected()` remains enabled for those rows. The user can therefore install/delete entries from a catalog the service itself rejected.
- **Likely root cause:** Catalog parsing mutates the published global model incrementally instead of validating into temporary state and committing atomically.
- **Impact:** A malformed or partially corrupted remote catalog can expose an arbitrary valid prefix as if it were authoritative, omit later families, and still allow package mutations despite the refresh reporting failure.
- **Repair direction:** Parse and validate the complete manifest into temporary `baseUrl`/family state, compute installed/update flags there, and swap it into the live catalog only after every entry succeeds. On failure either preserve the prior known-good catalog or expose zero actionable rows with an explicit failure state. Add a regression fixture with one valid family followed by a malformed family and verify no partial catalog is actionable.

- **Consolidation sources:** [automation/bug-scan-20260927-1723](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/39b791f3c4ed81d20c18406fdab65181fadca952/bugs.md); [automation/bug-scan-20260928-0518](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/a90cbab88e37ceee8eb4b37eceba4d93553fb2de/bugs.md); [automation/bug-scan-20260928-1726](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1e13b691f3ce2fc0e6c3a2b27decde7a2cbb8e7b/bugs.md); [automation/bug-scan-20260929-0526](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/ac10238dae4c53d985ef85309e535a23d7a98ff2/bugs.md); [automation/bug-scan-20260929-1917](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/3d4a529bc8f25364bbb487051cb8ef7c6c24c631/bugs.md); [Drive: 2026-09-27 17-23 MDT - automation-bug-scan-20260927-1723 - bugs.md diff](https://docs.google.com/spreadsheets/d/15nNwLnZgjZGSTTA2G_rH0Tfgz5nKQTc9MGOA3k9X-Zc/edit?usp=drivesdk); [Drive: 2026-09-28 05-18 MDT - automation-bug-scan-20260928-0518 - Instructions](https://docs.google.com/spreadsheets/d/1rCpCc1hG4ZhCTsjPSfGWl7UOdP6JOEQ2NfbOUGOL_1I/edit?usp=drivesdk); [Drive: 2026-09-28 05-18 MDT - automation-bug-scan-20260928-0518 - Diff](https://docs.google.com/spreadsheets/d/1WspiF_LH3fLG2UlbuLAFMUSAU-RD62l1EDg2_ZatVFU/edit?usp=drivesdk); [Drive: 2026-09-28 1726 MDT - automation-bug-scan-20260928-1726 - bugs.md diff](https://docs.google.com/spreadsheets/d/1kgrLWNI3FpH5CbvPkMidNJ1Rb8612YIc4wvcHUZTXAc/edit?usp=drivesdk); [Drive: 2026-09-29 0526 MDT - automation-bug-scan-20260929-0526 - Instructions](https://docs.google.com/document/d/1NDxyuhCjLL4X1lD0EejpL9Li6MgzBx8jZGDDc9BCkdk/edit?usp=drivesdk); [Drive: 2026-09-29 1917 MDT - automation-bug-scan-20260929-1917 - Instructions](https://docs.google.com/spreadsheets/d/1O5wp-eV76bDf7b1XTmWuqiwBkyZE41wo7vMZVGk59W8/edit?usp=drivesdk); [Drive: 2026-09-28 1726 MDT - automation-bug-scan-20260928-1726 - integration instructions](https://docs.google.com/spreadsheets/d/1L-AdwyFrRDz-RP_vMgrXG6VZH6JZ3DgdTcFwOdiQGSo/edit?usp=drivesdk); [Drive: 2026-09-28 1726 MDT - automation-bug-scan-20260928-1726 - integration instructions](https://docs.google.com/spreadsheets/d/102h_psLvf3PZUscY-JDkaum55n835woMdUanF829eTg/edit?usp=drivesdk); [Drive: 2026-09-29 0526 MDT - automation-bug-scan-20260929-0526 - Diff](https://docs.google.com/document/d/1crJqKG5skmKN8kGFAIAvJW_JYqZ2rpuPX2hec556r4U/edit?usp=drivesdk); [Drive: 2026-09-29 1917 MDT - automation-bug-scan-20260929-1917 - Diff](https://docs.google.com/spreadsheets/d/1v-2RxOKDnAfNG_X7UDymHq7idhxuopp2JE7PqASHHhc/edit?usp=drivesdk); [Drive: 2026-09-27 17-23 MDT - automation-bug-scan-20260927-1723 - integration instructions](https://docs.google.com/spreadsheets/d/1HIkqZ_HTDuK3KYas2pVz8jy0AUkqpcz4OpiipM8Cm9E/edit?usp=drivesdk)

### 77. A failed font update deletes the previously working installed family

- **Status:** Open.

- **Affected code:** `src/native/NativeFontBridge.cpp::installFamily()`; `src/network/HttpDownloader.cpp::downloadToFile()`; `src/FontInstaller.cpp::ensureFamilyDir()`, `buildFontPath()`, and `deleteFamily()`.
- **Trigger / reproduction:** Install a valid font family, publish an update for that family, then make any update file fail after installation starts (network interruption, short SD write, checksum mismatch, or validation failure). A multi-file family makes this especially easy by allowing an earlier file to succeed before a later one fails.
- **Observed / logically demonstrated failure:** For an already installed family, `ensureFamilyDir()` deliberately reuses the live family directory and `downloadToFile()` uses the final live filename, removing an existing destination before writing its replacement. On any subsequent download/CRC/validation failure, `installFamily()` calls `installer().deleteFamily()`, which removes the family directory from both font roots. Thus an update failure does not merely roll back the attempted update: it destroys the previously valid installed version. If the family was active, `deleteFamily()` also clears the live selected-font setting.
- **Likely root cause:** Update installation has no staging/commit boundary and uses the same destructive cleanup path for a fresh install and an update of an existing family.
- **Impact:** A transient network or SD error while updating fonts can permanently remove a working font family and unexpectedly switch the reader away from its active font.
- **Repair direction:** Download every update file into a transaction-specific staging directory, verify size/CRC/full font structure for the complete family, then atomically publish/swap the family while retaining the old directory until commit succeeds. On failure delete only staged artifacts; never delete the prior installed family. Add fault-injection tests for failure on the first and a later file of an existing family.

- **Consolidation sources:** [automation/bug-scan-20260927-1723](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/39b791f3c4ed81d20c18406fdab65181fadca952/bugs.md); [automation/bug-scan-20260928-0518](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/a90cbab88e37ceee8eb4b37eceba4d93553fb2de/bugs.md); [automation/bug-scan-20260928-1726](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1e13b691f3ce2fc0e6c3a2b27decde7a2cbb8e7b/bugs.md); [automation/bug-scan-20260929-0526](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/ac10238dae4c53d985ef85309e535a23d7a98ff2/bugs.md); [automation/bug-scan-20260929-1917](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/3d4a529bc8f25364bbb487051cb8ef7c6c24c631/bugs.md); [Drive: 2026-09-27 17-23 MDT - automation-bug-scan-20260927-1723 - bugs.md diff](https://docs.google.com/spreadsheets/d/15nNwLnZgjZGSTTA2G_rH0Tfgz5nKQTc9MGOA3k9X-Zc/edit?usp=drivesdk); [Drive: 2026-09-28 05-18 MDT - automation-bug-scan-20260928-0518 - Instructions](https://docs.google.com/spreadsheets/d/1rCpCc1hG4ZhCTsjPSfGWl7UOdP6JOEQ2NfbOUGOL_1I/edit?usp=drivesdk); [Drive: 2026-09-28 05-18 MDT - automation-bug-scan-20260928-0518 - Diff](https://docs.google.com/spreadsheets/d/1WspiF_LH3fLG2UlbuLAFMUSAU-RD62l1EDg2_ZatVFU/edit?usp=drivesdk); [Drive: 2026-09-28 1726 MDT - automation-bug-scan-20260928-1726 - bugs.md diff](https://docs.google.com/spreadsheets/d/1kgrLWNI3FpH5CbvPkMidNJ1Rb8612YIc4wvcHUZTXAc/edit?usp=drivesdk); [Drive: 2026-09-29 0526 MDT - automation-bug-scan-20260929-0526 - Instructions](https://docs.google.com/document/d/1NDxyuhCjLL4X1lD0EejpL9Li6MgzBx8jZGDDc9BCkdk/edit?usp=drivesdk); [Drive: 2026-09-29 1917 MDT - automation-bug-scan-20260929-1917 - Instructions](https://docs.google.com/spreadsheets/d/1O5wp-eV76bDf7b1XTmWuqiwBkyZE41wo7vMZVGk59W8/edit?usp=drivesdk); [Drive: 2026-09-28 1726 MDT - automation-bug-scan-20260928-1726 - integration instructions](https://docs.google.com/spreadsheets/d/1L-AdwyFrRDz-RP_vMgrXG6VZH6JZ3DgdTcFwOdiQGSo/edit?usp=drivesdk); [Drive: 2026-09-28 1726 MDT - automation-bug-scan-20260928-1726 - integration instructions](https://docs.google.com/spreadsheets/d/102h_psLvf3PZUscY-JDkaum55n835woMdUanF829eTg/edit?usp=drivesdk); [Drive: 2026-09-29 0526 MDT - automation-bug-scan-20260929-0526 - Diff](https://docs.google.com/document/d/1crJqKG5skmKN8kGFAIAvJW_JYqZ2rpuPX2hec556r4U/edit?usp=drivesdk); [Drive: 2026-09-29 1917 MDT - automation-bug-scan-20260929-1917 - Diff](https://docs.google.com/spreadsheets/d/1v-2RxOKDnAfNG_X7UDymHq7idhxuopp2JE7PqASHHhc/edit?usp=drivesdk); [Drive: 2026-09-27 17-23 MDT - automation-bug-scan-20260927-1723 - integration instructions](https://docs.google.com/spreadsheets/d/1HIkqZ_HTDuK3KYas2pVz8jy0AUkqpcz4OpiipM8Cm9E/edit?usp=drivesdk)

### 78. Valid long font family names are accepted but fixed path buffers silently truncate their install paths

- **Status:** Open.

- **Affected code:** `src/FontInstaller.cpp::isValidFamilyName()`, `ensureFamilyDir()`, and `buildFontPath()`; `src/native/NativeFontBridge.cpp::refreshCatalog()` and `installFamily()`; `lib/EpdFont/SdCardFontRegistry.cpp::parseFilename()` and discovery.
- **Trigger / reproduction:** Put a syntactically valid long family name in the font catalog. For example, a 55-character alphanumeric family with a conventional same-name `<family>_14.cpfont` file passes `isValidFamilyName()` and `isValidCpfontFilename()`, but the full `/.fonts/<family>/<filename>` path is longer than the 128-byte buffer passed to `FontInstaller::buildFontPath()`.
- **Observed / logically demonstrated failure:** Name validation imposes no length compatible with the later fixed path buffers. `buildFontPath()` uses `snprintf()` into `char path[128]` and its return value is ignored, so the destination is silently truncated. With the example above the final `.cpfont` suffix is cut off. The download, CRC, and magic-byte validation can all succeed against that truncated filename, so `installFamily()` returns success and sets `family.installed = true`. Registry discovery later rejects the file because its on-disk name no longer ends in `.cpfont`, leaving the supposedly installed family unavailable for selection. Longer names also reach separate 160-byte truncation points in family-directory/root lookup.
- **Likely root cause:** Accepted catalog identifier lengths are not derived from the storage/path ABI, and truncation from `snprintf()` is never checked.
- **Impact:** A catalog entry can report a successful font install while producing unreachable/misnamed files on SD; sufficiently long names can also make creation, lookup, and deletion operate on truncated path prefixes.
- **Repair direction:** Define explicit maximum family/file/path lengths, reject manifest entries that cannot be represented losslessly, and make all path builders return failure when `snprintf()` would truncate. Prefer `std::string` path construction followed by a single validated storage-path length check. Add boundary tests immediately below/at/above the maximum and verify discovered filenames exactly match the manifest.

- **Consolidation sources:** [automation/bug-scan-20260927-1723](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/39b791f3c4ed81d20c18406fdab65181fadca952/bugs.md); [Drive: 2026-09-27 17-23 MDT - automation-bug-scan-20260927-1723 - bugs.md diff](https://docs.google.com/spreadsheets/d/15nNwLnZgjZGSTTA2G_rH0Tfgz5nKQTc9MGOA3k9X-Zc/edit?usp=drivesdk); [Drive: 2026-09-27 17-23 MDT - automation-bug-scan-20260927-1723 - integration instructions](https://docs.google.com/spreadsheets/d/1HIkqZ_HTDuK3KYas2pVz8jy0AUkqpcz4OpiipM8Cm9E/edit?usp=drivesdk)

### 79. File Browser path mutations bypass reader metadata and strand recents/bookmarks

- **Status:** Open.
- **Affected code:** `Apps/file_browser.c`, `move_selected()` and the rename/delete paths in `consume_handoff_results()`; `src/native/NativePlatformBridge.cpp`, `renameFile()`; `src/native/NativeFileBrowserBridge.cpp`, `deleteDocument()`; metadata consumers in `src/RecentBooksStore.cpp` and `src/util/BookmarkUtil.cpp`.
- **Trigger / reproduction:** Open an EPUB so it appears in Recents and add at least one bookmark. In File Browser, rename it or move it to another SD folder. Return Home and try the Recent Books entry, then open the renamed/moved EPUB and inspect its bookmarks. Deleting a recently read EPUB demonstrates the stale-recents half of the same problem.
- **Observed / logically demonstrated failure:** File Browser implements rename and move as a bare `storage->rename_file(source, destination)`, whose firmware bridge is only `Storage.rename()`. It never calls the existing `RECENT_BOOKS.updatePath()` metadata migration used by `EpubReaderActivity::moveFinishedBookToReadFolder()`. Consequently a Recent Books record continues to reference the old pathname after a successful File Browser rename/move. Bookmark storage is also keyed from the EPUB pathname by `BookmarkUtil::getBookmarkPath()`, so the book at its new pathname looks for a different bookmark JSON file and its existing bookmarks appear lost while the old sidecar is orphaned. File Browser deletion clears the EPUB render cache but similarly does not remove the stale Recent Books record or bookmark sidecar.
- **Likely root cause:** Generic filesystem mutation APIs are being used as document-library operations without a transaction/coordinator that migrates or removes path-keyed reader metadata.
- **Impact:** Normal File Browser rename/move/delete operations can leave broken Home/Recents links, make valid bookmarks disappear from the renamed book, and accumulate orphaned metadata. The filesystem operation reports success even though the reader's logical library is inconsistent.
- **Repair direction:** Route document rename/move/delete through a metadata-aware operation that commits the filesystem change together with Recent Books path migration/removal, bookmark-sidecar migration/removal, cache migration/cleanup, and current-open state updates as applicable. Reuse the existing `RecentBooksStore::updatePath()` behavior and add rollback/error handling so a metadata failure cannot silently leave a half-migrated book. Add regression coverage for renamed, moved, and deleted EPUBs with both recents and bookmarks.

- **Consolidation sources:** [automation/bug-scan-20260927-1824](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/f18e5db9f21ba83e5d84c843b21ae9d6a519ad09/bugs.md); [Drive: 2026-09-27 18-24 MDT - automation-bug-scan-20260927-1824 - bugs.md diff](https://docs.google.com/spreadsheets/d/1oyjQXAi7QcRSrYPxqzxnC2zp9RtZnCBaDtMJRJNrEhU/edit?usp=drivesdk); [Drive: 2026-09-27 18-24 MDT - automation-bug-scan-20260927-1824 - integration instructions](https://docs.google.com/spreadsheets/d/1-mCapM3VmXXnX3n1ZU5Pj3vE0qq48e9grECoYtUTjm0/edit?usp=drivesdk)

### 80. Legacy plaintext credential conversion reports success when the obfuscated rewrite fails

- **Status:** Open.
- **Affected code:** `src/WifiCredentialStore.cpp::loadFromFile()`, `src/OpdsServerStore.cpp::loadFromFile()`, `lib/KOReaderSync/KOReaderCredentialStore.cpp::loadFromFile()`, and the `resave` contract in `src/JsonSettingsIO.cpp`.
- **Trigger / reproduction:** Start with a valid legacy JSON credential file containing the supported plaintext password field rather than the current obfuscated representation. Make the SD destination unwritable/full or inject a save failure while the store loads.
- **Observed / logically demonstrated failure:** Each JSON loader can return success with `resave=true` after accepting a legacy plaintext password. All three stores then call `saveToFile()` to rewrite the file using the current obfuscated field, but they ignore that return value and return the successful load result. Wi-Fi and OPDS log that they are resaving; KOReader likewise treats the load as complete. If the rewrite fails, the application proceeds normally while the plaintext credential remains on disk and the same migration must be attempted again next boot.
- **Likely root cause:** The format-upgrade write is treated as best-effort cleanup even though `resave` means the persisted credential file has not yet reached the current storage contract.
- **Impact:** Credentials that the firmware intends to migrate out of plaintext can remain plaintext indefinitely after a storage fault, with no propagated failure or reliable retry state. Logs/UI can imply a successful load/migration even though the at-rest representation was not upgraded.
- **Repair direction:** Treat a requested credential-format rewrite as a checked migration: only report migration success after the replacement is durably written, log failure distinctly, and either propagate a degraded/error result or retain an explicit retry-required state without discarding the loaded credentials. Use the atomic settings writer for the replacement and add injected-save-failure tests for Wi-Fi, OPDS, and KOReader legacy plaintext JSON.

- **Consolidation sources:** [automation/bug-scan-20260927-1824](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/f18e5db9f21ba83e5d84c843b21ae9d6a519ad09/bugs.md); [Drive: 2026-09-27 18-24 MDT - automation-bug-scan-20260927-1824 - bugs.md diff](https://docs.google.com/spreadsheets/d/1oyjQXAi7QcRSrYPxqzxnC2zp9RtZnCBaDtMJRJNrEhU/edit?usp=drivesdk); [Drive: 2026-09-27 18-24 MDT - automation-bug-scan-20260927-1824 - integration instructions](https://docs.google.com/spreadsheets/d/1-mCapM3VmXXnX3n1ZU5Pj3vE0qq48e9grECoYtUTjm0/edit?usp=drivesdk)

### 81. Bookmark add/delete UI commits RAM state and success feedback even when persistence fails

- **Status:** Open.
- **Affected code:** `src/activities/reader/EpubReaderActivity.cpp::addBookmark()` and `src/activities/reader/EpubReaderBookmarksActivity.cpp::deleteSelectedBookmark()`; persistence through `JsonSettingsIO::saveBookmarks()`.
- **Trigger / reproduction:** Open an EPUB, then induce an SD open/write failure while toggling a bookmark in the reader or deleting one from the Bookmarks activity.
- **Observed / logically demonstrated failure:** `addBookmark()` first mutates `cachedBookmarks` (insert or erase), sets the added/removed state used by the success popup, and only afterward calls `saveBookmarks()`; on failure it merely logs an error. `deleteSelectedBookmark()` similarly erases the bookmark from its live vector before saving, logs a failure without restoring it, then continues updating the selector and can even close the list as though the deletion succeeded. The user therefore sees the bookmark added/removed for the remainder of the session despite persistence having failed; after reload/reboot the prior on-disk state reappears (or, combined with a lower-level write failure, may be missing entirely).
- **Likely root cause:** Bookmark edits use mutate-then-save semantics and the UI has no failure state or rollback path.
- **Impact:** Bookmark state shown by the reader can diverge from durable state after an SD fault. Users can be told a bookmark was added or removed when that operation did not persist, leading to apparent bookmark loss/restoration across reopen or reboot.
- **Repair direction:** Apply edits to a candidate bookmark vector, persist it successfully, and only then publish the candidate to live state and show the success popup/list transition. On failure keep the prior vector and show an explicit error. Add fault-injection tests for both add/toggle and delete paths proving live state and UI remain unchanged when persistence fails.

- **Consolidation sources:** [automation/bug-scan-20260927-1824](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/f18e5db9f21ba83e5d84c843b21ae9d6a519ad09/bugs.md); [Drive: 2026-09-27 18-24 MDT - automation-bug-scan-20260927-1824 - bugs.md diff](https://docs.google.com/spreadsheets/d/1oyjQXAi7QcRSrYPxqzxnC2zp9RtZnCBaDtMJRJNrEhU/edit?usp=drivesdk); [Drive: 2026-09-27 18-24 MDT - automation-bug-scan-20260927-1824 - integration instructions](https://docs.google.com/spreadsheets/d/1-mCapM3VmXXnX3n1ZU5Pj3vE0qq48e9grECoYtUTjm0/edit?usp=drivesdk)

### 82. GT911 probe discards an I2C claim when cleanup fails

- **Status:** Open.
- **Affected code:** `Drivers/gt911_touch/driver.c`, `probe()`, `start()`, and `quiesce()`; the `i2c.bus@1` `release_device()` contract in `sdk/driver/RiscI2cBusV1.h`.
- **Trigger / reproduction:** Inject a GT911 probe failure after `claim_device()` succeeds—for example make the product/configuration read fail—then make `release_device()` return `false`. The same path is reachable when width/height validation or the status-register clear fails and claim release also fails.
- **Observed / logically demonstrated failure:** Both probe-failure branches call `release_device()` and explicitly discard its return value, then unconditionally set `bus_claim = 0`. The public I2C contract says release returns true only after all operations for the claim have drained, so a false return means the driver has not established that the claim is gone. Nevertheless the token is forgotten. `probe()` may then try the fallback GT911 address, and if startup ultimately fails `start()` clears the bus pointers with no claim token left for `quiesce()` to retry. The still-live claim is therefore orphaned.
- **Likely root cause:** Probe cleanup treats release as best-effort even though an I2C claim is an ownership resource that must remain tracked until release is confirmed.
- **Impact:** A transient I2C cleanup failure during touch-driver startup can leak a device claim, interfere with later touch/provider activation or I2C teardown, and require provider reload or reboot to recover. The failure path is especially damaging because the driver reports startup failure while silently losing the only handle that could complete cleanup.
- **Repair direction:** Never zero `bus_claim` unless `release_device()` succeeds. If probe cleanup fails, stop probing other addresses, preserve the bus API and claim token, enter a cleanup/fault state, and let `quiesce()` retry the release before unload. Add fault-injection tests for read/config-clear failure plus release failure and prove the claim remains tracked until a later successful release.

- **Consolidation sources:** [automation/bug-scan-20260927-1920](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/e0e57d1eb8c828d53468bff5916446d6ed7bd5d9/bugs.md); [Drive: 2026-09-27 19-20 MDT - automation-bug-scan-20260927-1920 - bugs.md diff](https://docs.google.com/spreadsheets/d/1Wj8E6gslpqIHpqTb-6cYinGRnzxsXlF1zdJEZOfu7JQ/edit?usp=drivesdk); [Drive: Copy of 2026-09-27 19-20 MDT - automation-bug-scan-20260927-1920 - integration instructions](https://docs.google.com/spreadsheets/d/1FW-pRVcHnhBnhj8ZlHiiAQauz9GsfAsv8Uv5jPIZEsY/edit?usp=drivesdk)

### 83. Ask Manifold's JSON string decoder can read past the response terminator on a short Unicode escape

- **Status:** Open.
- **Affected code:** `Apps/llm_ask.c`, `decode_json_string()` and `parse_hex4()`, on responses returned by `t5_network_api_v1::http_request()`.
- **Trigger / reproduction:** Return a non-truncated HTTP 2xx response whose selected `"content"` JSON string ends with an incomplete Unicode escape such as `"\\u"`, `"\\u1"`, or `"\\u12"` immediately before the response terminator. A malformed/truncated upstream JSON body that still fits the response buffer is sufficient.
- **Observed / logically demonstrated failure:** The network API guarantees a NUL-terminated response, but after seeing `\\u` the decoder calls `parse_hex4(quoted + i, ...)` without first proving four bytes remain before that NUL. `parse_hex4()` blindly indexes `s[0]` through `s[3]`. For an incomplete escape near the end of `response_json`, those reads continue past the logical response and can cross the 8192-byte array boundary. Because this parser handles network-provided bytes, malformed JSON can therefore drive an out-of-bounds read instead of simply producing the existing “Bad JSON from LLM7” error.
- **Likely root cause:** The hand-written JSON decoder validates each hex digit but not the available input length before fixed-width Unicode decoding.
- **Impact:** A malformed service response can cause undefined behavior in the Ask app and potentially crash the native-app task instead of failing closed. Short escapes can also consume stale bytes after the current NUL and misparse a response.
- **Repair direction:** Before every `\\uXXXX` decode, verify four non-NUL input bytes are available; do the same before looking ahead for a surrogate pair. Prefer passing an explicit response/string bound into the decoder rather than relying on sentinel reads. Reject incomplete escapes and lone/invalid surrogate sequences cleanly. Add parser tests for `\\u`, `\\u1`, `\\u12`, `\\u123`, escapes at the final buffer bytes, and valid surrogate pairs under ASan/UBSan.

- **Consolidation sources:** [automation/bug-scan-20260927-1920](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/e0e57d1eb8c828d53468bff5916446d6ed7bd5d9/bugs.md); [automation/bug-scan-20260930-0123](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/be3316ade6780966fd46791ae58d962ccbe0f03f/bugs.md); [Drive: 2026-09-27 19-20 MDT - automation-bug-scan-20260927-1920 - bugs.md diff](https://docs.google.com/spreadsheets/d/1Wj8E6gslpqIHpqTb-6cYinGRnzxsXlF1zdJEZOfu7JQ/edit?usp=drivesdk); [Drive: 2026-09-30 01-23 MDT - automation-bug-scan-20260930-0123 - Diff](https://docs.google.com/spreadsheets/d/1_WjxM50XfWlSay0EYe7KKqo0UsDms-ZGMu0yJ7YdWmQ/edit?usp=drivesdk); [Drive: Copy of 2026-09-27 19-20 MDT - automation-bug-scan-20260927-1920 - integration instructions](https://docs.google.com/spreadsheets/d/1FW-pRVcHnhBnhj8ZlHiiAQauz9GsfAsv8Uv5jPIZEsY/edit?usp=drivesdk); [Drive: 2026-09-30 01-23 MDT - automation-bug-scan-20260930-0123 - Instructions](https://docs.google.com/spreadsheets/d/1T-xVFt2LWX3ycdaLCcMfdzuq3AVXOovKHKSKdlD0LGg/edit?usp=drivesdk)

### 84. Risc Strike can miss XInput button presses because it drains reports only into a frame-rate snapshot

- **Status:** Open.
- **Affected code:** `Apps/risc_strike.c`, `fps_poll_controller()` and the main frame loop; `sdk/driver/RiscUsbHidV1.h`; `Drivers/usb_xinput_gamepad/driver.c::poll()` / `snapshot()`.
- **Trigger / reproduction:** Use the XInput provider and generate a press-and-release of RB, Start, or Select while both USB reports are queued before Risc Strike reaches its next controller sample. This is easy to provoke when a frame/render stalls long enough for both reports to arrive.
- **Observed / logically demonstrated failure:** The game polls generic input continuously, but it calls `fps_poll_controller()` only after the 33 ms frame deadline. That helper calls `g_xinput_api->poll(..., 8)`, which can drain several queued USB reports, and then reads only `snapshot()`. The gamepad ABI explicitly states that snapshot exposes current state and retains no historical button sequence; the XInput driver likewise documents “current state replaces earlier input.” If a down report and its later up report are both consumed by that one poll, the final snapshot is released. `controller_down = current & ~previous` is therefore zero and the discrete action is lost completely.
- **Likely root cause:** Edge-triggered game actions are reconstructed from a coalesced current-state snapshot after batching multiple reports, instead of sampling current state frequently enough or using a transition-preserving input path.
- **Impact:** Fire, pause/resume, start/restart, or exit presses can be intermittently ignored under display or scheduling latency, even though the USB driver received the reports correctly. This matches a particularly poor failure mode for an action game: input loss grows more likely exactly when rendering slows.
- **Repair direction:** Do not drain multiple XInput reports and then infer discrete edges solely from the final snapshot. Poll the provider on every short app-loop iteration and carry observed rising edges forward to the next simulation frame, or add/use an input API that preserves button transitions for action consumers while keeping movement state snapshot-based. Add a regression test where one provider poll consumes RB-down then RB-up before a render and verify exactly one fire action is delivered.

- **Consolidation sources:** [automation/bug-scan-20260927-1920](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/e0e57d1eb8c828d53468bff5916446d6ed7bd5d9/bugs.md); [Drive: 2026-09-27 19-20 MDT - automation-bug-scan-20260927-1920 - bugs.md diff](https://docs.google.com/spreadsheets/d/1Wj8E6gslpqIHpqTb-6cYinGRnzxsXlF1zdJEZOfu7JQ/edit?usp=drivesdk); [Drive: Copy of 2026-09-27 19-20 MDT - automation-bug-scan-20260927-1920 - integration instructions](https://docs.google.com/spreadsheets/d/1FW-pRVcHnhBnhj8ZlHiiAQauz9GsfAsv8Uv5jPIZEsY/edit?usp=drivesdk)

### 85. Failed display-takeover rollback can strand firmware touch capture

- **Status:** Open.
- **Affected code:** `src/native/NativeHardwareTakeover.cpp::native_hardware_takeover_begin()`; `nativeTouchSuspend()` / `nativeTouchResume()` ownership state.
- **Trigger / reproduction:** Start a display-takeover ELF when touch is available; let `nativeTouchSuspend()` succeed, force `display.suspendForExternalOwner()` to fail, and also make the rollback `nativeTouchResume()` fail.
- **Observed / logically demonstrated failure:** On display-suspend failure, the code calls `(void)nativeTouchResume()` but discards the result, then unconditionally sets `s_touch_borrowed = false` and returns `ESP_ERR_INVALID_STATE`. The takeover never starts, so `native_hardware_takeover_end()` will not perform the normal cleanup. If resume failed, firmware touch capture remains suspended while the only state recording that it needs restoration has been erased.
- **Likely root cause:** Rollback is best-effort rather than transactional; ownership state is cleared before recovery is confirmed.
- **Impact:** One compound takeover failure can leave touch input unavailable for the rest of the session/provider lifetime even though the ELF launch itself failed.
- **Repair direction:** Keep `s_touch_borrowed` set until `nativeTouchResume()` succeeds; retain a recoverable cleanup state and retry restoration before another launch or normal UI use. Add fault-injection coverage for successful touch suspend + display suspend failure + first resume failure, proving a later retry restores touch.

- **Consolidation sources:** [automation/bug-scan-20260927-2023](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/f7526a47d7d7457a76c24a54deae74b0fa8a844d/bugs.md); [Drive: 2026-09-27 20-23 MDT - automation-bug-scan-20260927-2023 - bugs.md diff](https://docs.google.com/spreadsheets/d/1pwBfN36zDI606vgnTlULMgciqheyddh1IPJL9qv8Urw/edit?usp=drivesdk); [Drive: 2026-09-27 20-23 MDT - automation-bug-scan-20260927-2023 - integration instructions](https://docs.google.com/spreadsheets/d/1m3OYCemYDfIjDsdrlFjIgLfI6tXGGdGfzEmXfxIqTdk/edit?usp=drivesdk)

### 86. Battery management never retries a transient first initialization failure

- **Status:** Open.
- **Affected code:** `lib/Board_T5S3/BoardT5S3.cpp::beginBatteryManagement()`, `configureBq25896()`, `configureBq27220()`, and `shutdownBatteryPower()`; surfaced through `src/native/NativeBatteryBridge.cpp::readState()` and `Apps/battery.c`.
- **Trigger / reproduction:** Make the first charger or fuel-gauge initialization fail because of a transient I2C/NACK/startup condition, then allow the device/bus to recover and refresh Battery Status or later request battery power shutdown.
- **Observed / logically demonstrated failure:** `beginBatteryManagement()` sets `batteryInitAttempted = true` before either component initializes. Every later call returns only the cached `bq25896Ready || bq27220Ready` and never reruns either failed initializer. If both fail once, Battery Status stays unavailable despite recovery; if only one fails, that component's telemetry/functionality is missing permanently. If BQ25896 was the failed component, `shutdownBatteryPower()` cannot recover in that boot.
- **Likely root cause:** A one-shot "attempted" latch is used as if it represented successful initialization, rather than tracking and retrying each independently recoverable component.
- **Impact:** A brief boot-time I2C/device-readiness glitch can disable battery telemetry, charger state, or hard-power-off functionality until reboot.
- **Repair direction:** Track readiness per component and retry only failed initializers after their existing failure cleanup, with bounded/backoff retry if desired. Do not suppress a later attempt solely because an earlier attempt ran. Add tests for first-failure/second-success and charger-success/gauge-failure (and inverse) recovery.

- **Consolidation sources:** [automation/bug-scan-20260927-2023](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/f7526a47d7d7457a76c24a54deae74b0fa8a844d/bugs.md); [automation/bug-scan-20260929-2120](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/82bef6c8270ad0ed072369ee19b07022dc82e181/bugs.md); [Drive: 2026-09-27 20-23 MDT - automation-bug-scan-20260927-2023 - bugs.md diff](https://docs.google.com/spreadsheets/d/1pwBfN36zDI606vgnTlULMgciqheyddh1IPJL9qv8Urw/edit?usp=drivesdk); [Drive: 2026-09-29 2120 MDT - automation-bug-scan-20260929-2120 - Instructions](https://docs.google.com/spreadsheets/d/1uI1Z04FNBkqdLQxhgZuCSZXUFQbOTUOFJ7dzYn39FoE/edit?usp=drivesdk); [Drive: 2026-09-29 2120 MDT - automation-bug-scan-20260929-2120 - Instructions](https://docs.google.com/document/d/1xv-O64ISDIO8zYQ7r6A9vc7x-RMMWrApoVqtPFzcB1U/edit?usp=drivesdk); [Drive: 2026-09-29 2120 MDT - automation-bug-scan-20260929-2120 - Diff](https://docs.google.com/spreadsheets/d/1v0enkcChAVBR3-RFLlrRcIS8MhmEQyj1AV0LlBx-aj4/edit?usp=drivesdk); [Drive: 2026-09-29 2120 MDT - automation-bug-scan-20260929-2120 - Diff](https://docs.google.com/spreadsheets/d/1Dq6s7qm3fIlKrMX_AVJaMlTG3V-Vfhf-Onk_qRqtUog/edit?usp=drivesdk); [Drive: 2026-09-29 2120 MDT - automation-bug-scan-20260929-2120 - Diff](https://docs.google.com/document/d/1Igvgxgg6gX8_I8-QVjW_Ve2n06-Dp58Adt6XIthpikI/edit?usp=drivesdk); [Drive: 2026-09-27 20-23 MDT - automation-bug-scan-20260927-2023 - integration instructions](https://docs.google.com/spreadsheets/d/1m3OYCemYDfIjDsdrlFjIgLfI6tXGGdGfzEmXfxIqTdk/edit?usp=drivesdk)

### 87. Fast-video teardown can restore the firmware display while the scan task is still alive

- **Status:** Open.
- **Affected code:** `src/native/NativeVideoBridge.cpp::epd_video_shutdown()`, `video_stop()`, `wait_for_dma()`, and `scan_task()`; `src/native/NativeHardwareTakeover.cpp::native_hardware_takeover_end()`.
- **Trigger / reproduction:** Start a display-takeover app, then make the raw EPD scan task fail to terminate after `g_running = false`—for example strand it in an outstanding DMA wait/callback path—so `g_scan_task` is still non-null after the 200 × 10 ms shutdown wait.
- **Observed / logically demonstrated failure:** `epd_video_shutdown()` only logs "scan task did not stop before video teardown" and returns without releasing the panel IO/bus, buffers, or scan task. `video_stop()` has no status and nevertheless sets `s_video_started = false`. The takeover-end path then clears `s_display_borrowed` and calls `display.resumeFromExternalOwner()`. Firmware display ownership can therefore be restored while the raw scan task and its hardware resources are still active.
- **Likely root cause:** Teardown failure is not represented in the API/state machine; a void shutdown timeout is treated as success by the caller.
- **Impact:** Firmware and the orphaned scan engine can concurrently own panel GPIO/i80 resources, causing a wedged or corrupted display, resource conflicts, or a crash. Later video starts can also see stale bus/task state despite `s_video_started` being false.
- **Repair direction:** Make shutdown return explicit success/failure and keep `s_video_started` plus hardware-takeover ownership asserted until task/DMA teardown is confirmed. Add a bounded forced-cancellation/reset path for wedged scan/DMA state before firmware display restoration. Fault-inject a non-terminating scan task/callback and verify firmware display resume is blocked until cleanup succeeds.

- **Consolidation sources:** [automation/bug-scan-20260927-2023](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/f7526a47d7d7457a76c24a54deae74b0fa8a844d/bugs.md); [Drive: 2026-09-27 20-23 MDT - automation-bug-scan-20260927-2023 - bugs.md diff](https://docs.google.com/spreadsheets/d/1pwBfN36zDI606vgnTlULMgciqheyddh1IPJL9qv8Urw/edit?usp=drivesdk); [Drive: 2026-09-27 20-23 MDT - automation-bug-scan-20260927-2023 - integration instructions](https://docs.google.com/spreadsheets/d/1m3OYCemYDfIjDsdrlFjIgLfI6tXGGdGfzEmXfxIqTdk/edit?usp=drivesdk)

### 88. Shared GPS/LoRa rail can remain powered after a failed final release

- **Status:** Open.

- **Affected code:** `src/runtime/resources/RadioPower.cpp`, `RadioPower::acquire()`, `RadioPower::release()`, and `setRail()`; consumers include `src/runtime/drivers/GpsKernelIo.cpp` and `src/native/NativeLoRaBridge.cpp`.
- **Trigger / reproduction:** Let GPS or LoRa be the final owner of the shared `PCA9535_IO00_LORA_GPS_EN` rail, then inject an I2C/PCA9535 failure while `RadioPower::release()` is disabling that rail. A related partial-enable case is a successful output-latch write followed by failure to configure the expander pin direction.
- **Observed / logically demonstrated failure:** `release()` clears the final owner bit before calling `setRail(false)`, discards the return value, and returns `void`. If the physical disable fails, the software state becomes `owners == 0` even though the GPS/LoRa rail can still be energized, so neither caller nor the power manager retains a cleanup obligation or retry state. Conversely, a partially successful `setRail(true)` can energize the rail while `acquire()` returns failure without recording an owner.
- **Likely root cause:** Software ownership is committed independently of the two-step PCA9535 hardware transition, and failed transitions are not represented as a retryable intermediate state.
- **Impact:** GPS/LoRa hardware can remain powered after both services believe they are stopped, causing avoidable battery drain and leaving powered peripheral pins active. A failed enable can create the same software/hardware ownership mismatch.
- **Repair direction:** Make the rail transition transactional and retryable. Do not clear the final owner until the disable is confirmed; retain a pending-cleanup state when an I2C operation fails, expose/propagate cleanup failure, and roll back partial enables before reporting failure. Add fault-injection tests for each PCA9535 operation in both first-acquire and final-release paths.

- **Consolidation sources:** [automation/bug-scan-20260927-2123](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/a737a5b12df4cbf60e166f382a9c4a7a5385b398/bugs.md); [automation/bug-scan-20260928-1321](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b5bb23216aff4f9d1565fe127c00e99f3c8e268b/bugs.md); [Drive: 2026-09-28 1321 MDT - automation-bug-scan-20260928-1321 - Instructions](https://docs.google.com/spreadsheets/d/1SD5vp5rxtrL_Mp80xBG_6bKaAVb5R0WaeVM6gS3-Tzs/edit?usp=drivesdk); [Drive: 2026-09-28 1321 MDT - automation-bug-scan-20260928-1321 - Diff](https://docs.google.com/spreadsheets/d/1C7xQuBintsL2WCAzJohJICs00HGoUDs5ir8iAm0eTCg/edit?usp=drivesdk); [Drive: 2026-09-27 21-23 MDT - automation-bug-scan-20260927-2123 - bugs.md diff](https://docs.google.com/spreadsheets/d/1Mfd_met-jgMqbD_jeBmk9rbcukEbm5fFNqsS9Q_WJwM/edit?usp=drivesdk); [Drive: 2026-09-27 21-23 MDT - automation-bug-scan-20260927-2123 - integration instructions](https://docs.google.com/spreadsheets/d/1IcyJZWNPoXlj00LOuW6tjjHpYpe4AuC9Jps4Mvjc32g/edit?usp=drivesdk)

### 89. Recursive directory deletion can construct the wrong child path when an SdFat name exceeds its 128-byte buffer

- **Status:** Open.

- **Affected code:** `lib/hal/HalStorage.cpp`, `removeDirUnlocked()`; the same unchecked fixed-name-buffer pattern also appears in `HalStorage::listFiles()`.
- **Trigger / reproduction:** Create a directory containing a valid FAT long filename or UTF-8 filename whose encoded name does not fit in `char name[128]`, then delete the parent through any path that reaches `Storage.removeDir()`.
- **Observed / logically demonstrated failure:** `removeDirUnlocked()` calls `file.getName(name, sizeof(name))` but ignores its success/length result and immediately appends `name` to the parent path. When SdFat cannot return the complete name, the stack buffer is not a verified child name; deletion can therefore address stale/truncated/undefined path text rather than the opened child. The operation may fail partway through, recurse into the wrong path, or—if the stale text resolves to another sibling—remove an unintended sibling while leaving the actual long-name entry behind.
- **Likely root cause:** The recursive destructive path assumes every filesystem name fits a 127-byte C string and treats `getName()` as infallible.
- **Impact:** Deleting a directory containing long names is not reliable and has a data-loss risk because path-based removal is performed from an unvalidated name buffer.
- **Repair direction:** Check the `getName()` return value before constructing any path and use storage sized for the filesystem's supported UTF-8 long filename length (or operate on the opened child handle where possible). Fail closed without issuing a remove when a complete name cannot be obtained. Add recursive-delete tests with long ASCII and multibyte UTF-8 names, plus a sibling whose name would expose stale-buffer reuse.

- **Consolidation sources:** [automation/bug-scan-20260927-2123](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/a737a5b12df4cbf60e166f382a9c4a7a5385b398/bugs.md); [automation/bug-scan-20260929-1917](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/3d4a529bc8f25364bbb487051cb8ef7c6c24c631/bugs.md); [Drive: 2026-09-27 21-23 MDT - automation-bug-scan-20260927-2123 - bugs.md diff](https://docs.google.com/spreadsheets/d/1Mfd_met-jgMqbD_jeBmk9rbcukEbm5fFNqsS9Q_WJwM/edit?usp=drivesdk); [Drive: 2026-09-29 1917 MDT - automation-bug-scan-20260929-1917 - Instructions](https://docs.google.com/spreadsheets/d/1O5wp-eV76bDf7b1XTmWuqiwBkyZE41wo7vMZVGk59W8/edit?usp=drivesdk); [Drive: 2026-09-29 1917 MDT - automation-bug-scan-20260929-1917 - Diff](https://docs.google.com/spreadsheets/d/1v-2RxOKDnAfNG_X7UDymHq7idhxuopp2JE7PqASHHhc/edit?usp=drivesdk); [Drive: 2026-09-27 21-23 MDT - automation-bug-scan-20260927-2123 - integration instructions](https://docs.google.com/spreadsheets/d/1IcyJZWNPoXlj00LOuW6tjjHpYpe4AuC9Jps4Mvjc32g/edit?usp=drivesdk)

### 90. GPS module unload failure is untracked while its ELF handle and package pin are retained

- **Status:** Open.

- **Affected code:** `src/runtime/drivers/GpsDriverRuntime.cpp`, `GpsDriverRuntime::stop()`; `src/runtime/drivers/GpsDriverModule.cpp`, `GpsDriverModule::stop()` and `start()`; package lifetime through `RuntimePackages::systemPackageUseGate()`.
- **Trigger / reproduction:** Start the `gps-nmea` provider, then inject a `dlclose()` failure (or package-use-gate unpin failure) when `GpsDriverRuntime::stop()` runs at app/provider teardown.
- **Observed / logically demonstrated failure:** `GpsDriverModule::stop()` deliberately returns false while retaining a failed module's mapped handle and, for a failed `dlclose()`, its package pin. `GpsDriverRuntime::stop()` only logs that failure, then releases the UART/device lease, clears `owner`, `positionLease`, and `invocation`, and untracks the execution-context `GnssDriver` cleanup resource. The retained mapped/pinned module is therefore no longer represented by the runtime cleanup state. `GpsDriverModule::start()` explicitly refuses to load while `handle_` remains non-null, so a persistent close failure can wedge future GPS starts; the retained package pin can also block driver package replacement.
- **Likely root cause:** The outer runtime treats module teardown as best-effort even though the module's failure contract intentionally retains resources that require a later cleanup retry.
- **Impact:** A single unload failure can leave `gps-nmea` mapped/pinned after the owning invocation has been forgotten, preventing GPS reuse or driver update until reboot and defeating the package-lifetime safety mechanism's retry semantics.
- **Repair direction:** Keep a retryable firmware-owned cleanup/quarantine record until `GpsDriverModule` reaches `Absent`; do not clear/untrack the last cleanup responsibility merely because physical UART/lease release succeeded. Retry close/unpin from a safe owner context, and add fault-injection tests for `dlclose` and package-unpin failures that prove later cleanup completes without requiring a new GPS session.

- **Consolidation sources:** [automation/bug-scan-20260927-2123](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/a737a5b12df4cbf60e166f382a9c4a7a5385b398/bugs.md); [Drive: 2026-09-27 21-23 MDT - automation-bug-scan-20260927-2123 - bugs.md diff](https://docs.google.com/spreadsheets/d/1Mfd_met-jgMqbD_jeBmk9rbcukEbm5fFNqsS9Q_WJwM/edit?usp=drivesdk); [Drive: 2026-09-27 21-23 MDT - automation-bug-scan-20260927-2123 - integration instructions](https://docs.google.com/spreadsheets/d/1IcyJZWNPoXlj00LOuW6tjjHpYpe4AuC9Jps4Mvjc32g/edit?usp=drivesdk)

### 91. GT911 drops a ready touch report after a transient contact-data read failure

- **Status:** Open.
- **Affected code:** `Drivers/gt911_touch/driver.c`, especially `service_one()`, `read_reg()`, `write_reg8()`, and the handoff into `apply_state()`.
- **Trigger / reproduction:** Have the GT911 report `GT911_READY_MASK` with one or more contacts, then fault the I2C transaction that reads `GT911_FIRST_POINT_REG` while allowing the following write of zero to `GT911_STATUS_REG` to succeed. This can be reproduced with an injected one-shot failure in `bus->transact()` for the contact-data read.
- **Observed / logically demonstrated failure:** When the raw contact read fails, `service_one()` executes `(void)write_reg8(GT911_STATUS_REG, 0u)` and returns `false`. Clearing the status acknowledges and discards the controller's ready report even though its contents were never acquired or passed to `apply_state()`. A missed DOWN can disappear until another report happens; more seriously, if the previous published snapshot contained a contact and the discarded report was its changed/released state, subscribers and `snapshot()` can retain stale contact state with no event that repairs it.
- **Likely root cause:** The error path acknowledges a report before the driver has a coherent replacement state. The normal path intentionally acknowledges before publication to avoid duplicate delivery, but the failed-read path has nothing valid to publish and therefore destroys the only retryable copy of the report.
- **Impact:** A transient I2C error can become a lost tap, missed motion, or apparently stuck touch rather than a recoverable failed poll. UI consumers can remain out of sync with the physical panel until the GT911 produces another valid report.
- **Repair direction:** Do not clear `GT911_STATUS_REG` when the contact-data read fails; leave the ready report pending so a later poll can retry it. For malformed reports that must be discarded, explicitly invalidate/reconcile published contact state and subscriber queues rather than silently retaining the old snapshot. Add a fault-injection test that fails the first contact-data read, succeeds the retry, and proves exactly one coherent transition is published.

- **Consolidation sources:** [automation/bug-scan-20260927-2219](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/9660dd8d0666b426983297016ead4505192f218d/bugs.md); [Drive: 2026-09-27 22-19 MDT - automation-bug-scan-20260927-2219 - bugs.md diff](https://docs.google.com/document/d/1Wc7kIceAGc40Fe2AsE_kUn2qH-_Qeo0f2PmKF0JnlCw/edit?usp=drivesdk); [Drive: 2026-09-27 22-19 MDT - automation-bug-scan-20260927-2219 - integration instructions](https://docs.google.com/document/d/1V2MnDTGfJQQ0NgxgrN3NEn3zX-ta4QCM7qWP9tzy9BU/edit?usp=drivesdk)

### 92. USB mass-storage file creation leaves orphaned FAT long-name entries after a directory write failure

- **Status:** Open.
- **Affected code:** `Drivers/usb_mass_storage/driver.c`, `create_file_entry()`, `write_directory_entry()`, `find_free_directory_slots()`, and the `volume_file_open_write()` creation path.
- **Trigger / reproduction:** Copy a file to FAT USB storage using a name that requires multiple VFAT LFN entries, then inject a sector-write failure after one or more LFN directory entries have been written but before the remaining LFN entries or final short 8.3 entry are committed.
- **Observed / logically demonstrated failure:** `create_file_entry()` writes each LFN entry directly to the live directory and immediately returns `false` on the first later write failure. It never marks the entries already written by this attempt as deleted. No file handle is returned, so `volume_file_close(..., commit=false)` cannot clean them up. Those persistent orphan LFN records are treated as occupied slots by future scans; repeated failed creates can consume directory capacity, and a stray LFN sequence can also be associated with a later short entry if its ordering/checksum happens to match.
- **Likely root cause:** Multi-entry FAT namespace publication is incremental but has no rollback record for entries successfully written before the operation fails.
- **Impact:** A transient USB/media write error can permanently pollute the directory even though File Browser reports that file creation failed. Repetition can make a directory appear full or produce misleading long-name metadata without corresponding files.
- **Repair direction:** Track the starting slot and number of directory entries successfully published. If any later LFN or short-entry write fails, best-effort mark every entry written by that attempt deleted before returning failure; preserve/restore an end-of-directory marker when the allocation consumed one. Add fault-injection tests for failure at every entry boundary and verify that a subsequent directory scan contains no residue from the failed create.

- **Consolidation sources:** [automation/bug-scan-20260927-2219](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/9660dd8d0666b426983297016ead4505192f218d/bugs.md); [Drive: 2026-09-27 22-19 MDT - automation-bug-scan-20260927-2219 - bugs.md diff](https://docs.google.com/document/d/1Wc7kIceAGc40Fe2AsE_kUn2qH-_Qeo0f2PmKF0JnlCw/edit?usp=drivesdk); [Drive: 2026-09-27 22-19 MDT - automation-bug-scan-20260927-2219 - integration instructions](https://docs.google.com/document/d/1V2MnDTGfJQQ0NgxgrN3NEn3zX-ta4QCM7qWP9tzy9BU/edit?usp=drivesdk)

### 93. USB mass-storage accepts a BPB whose FAT is too small for its declared cluster count and can write into data sectors

- **Status:** Open.
- **Affected code:** `Drivers/usb_mass_storage/driver.c`, primarily `parse_bpb()`, with destructive consequences in `fat_get()`, `fat_set()`, `allocate_cluster()`, and `free_chain()`.
- **Trigger / reproduction:** Present a FAT16 or FAT32 volume whose boot sector has otherwise valid geometry/signatures but declares a `fat_size` too small to contain one FAT entry for every cluster implied by total sectors and sectors-per-cluster. The current parser checks that reserved/FAT/root regions leave data sectors, but never checks FAT entry capacity against `cluster_count`.
- **Observed / logically demonstrated failure:** The malformed volume can mount successfully. Later, `fat_get()` and `fat_set()` compute an offset from the cluster number and add it to `fat_start_lba` without verifying that the resulting sector remains inside `fat_sectors`. Accessing a sufficiently high cluster therefore reads beyond the FAT, and allocation/free operations can write FAT values into the following root-directory or data area.
- **Likely root cause:** `parse_bpb()` validates overall layout size but omits the core invariant that each FAT copy must be large enough for at least `cluster_count + 2` FAT entries of the selected FAT width.
- **Impact:** A damaged or crafted USB filesystem can turn an attempted copy/delete into on-media corruption outside the FAT itself, potentially damaging directory entries or user file data.
- **Repair direction:** After determining FAT16 versus FAT32 and before publishing mounted geometry, verify that `fat_size * 512` can represent at least `cluster_count + 2` entries (2 bytes each for FAT16, 4 bytes each for FAT32) and that every FAT copy remains within the validated reserved FAT region. Reject the mount if the invariant fails. Add malformed-BPB tests where current layout checks pass but FAT capacity is one or more entries short.

- **Consolidation sources:** [automation/bug-scan-20260927-2219](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/9660dd8d0666b426983297016ead4505192f218d/bugs.md); [Drive: 2026-09-27 22-19 MDT - automation-bug-scan-20260927-2219 - bugs.md diff](https://docs.google.com/document/d/1Wc7kIceAGc40Fe2AsE_kUn2qH-_Qeo0f2PmKF0JnlCw/edit?usp=drivesdk); [Drive: 2026-09-27 22-19 MDT - automation-bug-scan-20260927-2219 - integration instructions](https://docs.google.com/document/d/1V2MnDTGfJQQ0NgxgrN3NEn3zX-ta4QCM7qWP9tzy9BU/edit?usp=drivesdk)

### 94. Navigation activation failure can permanently disable firmware navigation after a transient handoff error

- **Status:** Open.
- **Affected code:** `src/native/NativeNavigationInput.cpp`, especially `ready()`, `nativeNavigationTick()`, `nativeNavigationRetry()`, and `nativeNavigationBoundary()`; `src/runtime/input/NavigationFocus.h::NavigationFocus::apply()`.
- **Trigger / reproduction:** Install a valid `input.navigation` provider and let `RuntimeInstalledProviders::acquireCapability()` succeed, then inject a one-shot failure from either the provider's `foreground()` call reached through `focus.apply(api)` or its first `reset()` call during `ready()`. Allow later foreground/reset calls to succeed and continue ticking firmware navigation normally.
- **Observed / logically demonstrated failure:** `ready()` stores `api = candidate` before running `focus.apply(api) && api->reset(...)`. When either handoff operation fails, it leaves both the API pointer and provider lease installed while setting `usable = false`. Every later `ready()` immediately returns true because `api` is non-null, while `nativeNavigationTick()` rejects the frame because `usable` is still false. `nativeNavigationRetry()` also refuses to reset its retry state whenever `api` is non-null. A transient activation failure therefore has no normal retry path and navigation remains unavailable until a separate suspend/reconfigure/reboot path tears the provider down.
- **Likely root cause:** Provider publication is not transactional: the acquired API/lease are committed to global state before the foreground handoff and initial reset have succeeded, but the failure path neither rolls them back nor schedules a retry of those operations.
- **Impact:** Keyboard/controller navigation for firmware UI can become permanently unavailable for the session after one transient provider handoff/reset error. The retained grant can also keep the provider graph active and interfere with driver replacement or sleep/quiescence workflows.
- **Repair direction:** Keep the acquired lease/API local until `focus.apply()` and `reset()` both succeed, or explicitly roll back/retain a retryable cleanup state on failure and clear the global API so the existing bounded activation retry can run. Add failure-injection tests where `foreground()` and `reset()` fail once and then succeed, proving navigation recovers without reboot and no grant is leaked.

- **Consolidation sources:** [automation/bug-scan-20260927-2329](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/d47f3c6d9e5e9d4befae8161565f10f2bf7ac9f2/bugs.md); [automation/bug-scan-20260928-1321](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b5bb23216aff4f9d1565fe127c00e99f3c8e268b/bugs.md); [Drive: 2026-09-28 1321 MDT - automation-bug-scan-20260928-1321 - Instructions](https://docs.google.com/spreadsheets/d/1SD5vp5rxtrL_Mp80xBG_6bKaAVb5R0WaeVM6gS3-Tzs/edit?usp=drivesdk); [Drive: 2026-09-28 1321 MDT - automation-bug-scan-20260928-1321 - Diff](https://docs.google.com/spreadsheets/d/1C7xQuBintsL2WCAzJohJICs00HGoUDs5ir8iAm0eTCg/edit?usp=drivesdk); [Drive: 2026-09-27 23-29 MDT - automation-bug-scan-20260927-2329 - Diff](https://docs.google.com/spreadsheets/d/1UiMQv6PT5i3C5JSql-H4o-mdjzVsqIubpQjmOxFeo1k/edit?usp=drivesdk); [Drive: 2026-09-27 23-29 MDT - automation-bug-scan-20260927-2329 - Instructions](https://docs.google.com/spreadsheets/d/1nMF7-NCdTvVw6nFSxY5Nf9OsjEyDbH7gwpxWc0h8CPI/edit?usp=drivesdk)

### 95. File-association rebuild persists a partial registry after an Apps directory scan failure

- **Status:** Open.
- **Affected code:** `src/native/FileAssociationRegistry.cpp`, especially `NativeFileAssociations::rebuild()`; consumers include `src/native/NativeFileOpenBridge.cpp` and File Browser Open-with resolution.
- **Trigger / reproduction:** Start with multiple valid application handlers under `/Apps` and an existing complete file-association registry. Force `Storage.open("/Apps", O_RDONLY)` to fail, or force `openNextFile()` to stop with an SD/enumeration error after only a prefix of the directory has been returned, then invoke an association refresh.
- **Observed / logically demonstrated failure:** `rebuild()` immediately clears the live `handlers` vector and `loaded` flag, adds only the built-in reader, and then treats a failed/invalid directory entry exactly like end-of-directory. It subsequently sorts that incomplete set, sets `loaded = true`, persists it to `/System/Registry/FileAssociations.json`, and returns success. Valid handlers that were not reached by the failed scan therefore disappear from both the live registry and the persisted derived index.
- **Likely root cause:** The rebuild is destructive and non-transactional, and the directory API result is used as EOF without proving enumeration completed successfully.
- **Impact:** A transient SD or directory-enumeration fault can make installed applications disappear from Open-with handling, select the wrong remaining handler, and preserve the incomplete association set until another successful rebuild.
- **Repair direction:** Build a candidate registry in temporary storage, distinguish verified enumeration completion from an I/O failure, and atomically replace/persist the live registry only after the complete scan succeeds. Preserve the previous registry on failure. Add fault-injection coverage for failure before the first app and midway through a multi-app scan.

- **Consolidation sources:** [automation/bug-scan-20260927-2329](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/d47f3c6d9e5e9d4befae8161565f10f2bf7ac9f2/bugs.md); [automation/bug-scan-20260929-0623-final](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/a0eeba8b922c599a6b21700ab44037089cb37b92/bugs.md); [automation/bug-scan-20260929-0623-findings](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b1463ab6ef1fc1a6345ea660e56ae484b98b07b9/bugs.md); [automation/bug-scan-20260929-1823](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/66efc35761d7ed6bc0cf53c7b151ba6533e03035/bugs.md); [automation/bug-scan-20260930-0518](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/82533918d81fda11ae9c6b19d95b071a98f19361/bugs.md); [Drive: 2026-09-30 0518 MDT - automation-bug-scan-20260930-0518 - Diff](https://docs.google.com/spreadsheets/d/1geT64642Lsjo_EaRZhNxF9516LV2BRVIp4stNAnOkjg/edit?usp=drivesdk); [Drive: 2026-09-29 0623 MDT - automation-bug-scan-20260929-0623-final - Instructions](https://docs.google.com/spreadsheets/d/1_in6hCljLxqiyI5MrjIzkrraPXJuEllimsyAhbStfjQ/edit?usp=drivesdk); [Drive: 2026-09-29 0623 MDT - automation-bug-scan-20260929-0623-findings - Instructions](https://docs.google.com/document/d/1MFdtmLQczvKYtfqT4o_4BxJt-W0lax5qxpBBYoeyBcQ/edit?usp=drivesdk); [Drive: 2026-09-27 23-29 MDT - automation-bug-scan-20260927-2329 - Diff](https://docs.google.com/spreadsheets/d/1UiMQv6PT5i3C5JSql-H4o-mdjzVsqIubpQjmOxFeo1k/edit?usp=drivesdk); [Drive: 2026-09-29 1823 MDT - automation_bug-scan-20260929-1823 - Instructions](https://docs.google.com/spreadsheets/d/14Xk8HtTwY5BtOEJKKftF1HZCL0C3nEKCpxpxHUg2cmI/edit?usp=drivesdk); [Drive: 2026-09-29 0623 MDT - automation-bug-scan-20260929-0623-findings - Diff](https://docs.google.com/document/d/1tx4_PKHXrJvCuZyT8yr6fDOBHKQ1W_l_5VsZu7TB7xw/edit?usp=drivesdk); [Drive: 2026-09-29 1823 MDT - automation_bug-scan-20260929-1823 - Diff](https://docs.google.com/spreadsheets/d/1TPhDZw8-Vhfak_h1pT0hJdvu40fJKaskrXyf1AbUnas/edit?usp=drivesdk); [Drive: 2026-09-27 23-29 MDT - automation-bug-scan-20260927-2329 - Instructions](https://docs.google.com/spreadsheets/d/1nMF7-NCdTvVw6nFSxY5Nf9OsjEyDbH7gwpxWc0h8CPI/edit?usp=drivesdk); [Drive: 2026-09-30 0518 MDT - automation-bug-scan-20260930-0518 - Instructions](https://docs.google.com/spreadsheets/d/1xB4JDjKoHfw-mCY9pUQL1VP2tWxilaT0WRwdCPryrO8/edit?usp=drivesdk)

### 96. Invalid text-input provider cleanup can orphan a provider generation after release failure

- **Status:** Open.
- **Affected code:** `src/native/NativeTextInput.cpp::nativeTextInputBegin()`; provider lifetime machinery in `src/runtime/drivers/InstalledProviderGraph.cpp::release()` and `src/runtime/drivers/ProviderGraphV2.cpp::release()`.
- **Trigger / reproduction:** Make `RuntimeInstalledProviders::acquireCapability("input.text", ...)` return a provider generation whose interface fails `NativeTextInput::valid()` (for example a mismatched API version/struct or missing required entry point), then inject a teardown/quiesce failure so `RuntimeInstalledProviders::release(&acquired)` returns false.
- **Observed / logically demonstrated failure:** The invalid-interface branch calls `(void)RuntimeInstalledProviders::release(&acquired)` and unconditionally returns. The release wrapper clears its `Lease` only on success; on failure the exact generation remains represented by the local `acquired` value. Because that value then goes out of scope and the global `lease`, `api`, and `navigationClaimed` state were never populated, `nativeTextInputEnd()` believes there is nothing to clean up and a later `nativeTextInputBegin()` may start a fresh acquisition instead of completing teardown of the failed generation. The open U1 provider-graph work makes failed releases explicitly retryable, but this caller still discards the retry token.
- **Likely root cause:** The API-validation failure path assumes provider release cannot fail and does not transfer a failed cleanup obligation into persistent state.
- **Impact:** A malformed/broken text-input provider plus one cleanup failure can leave a provider module/dependency generation pinned or quarantined without a firmware-owned retry path, blocking later keyboard activation, provider replacement, graph shutdown, or hardware reuse until reboot/recovery.
- **Repair direction:** On validation failure, check the release result. If teardown fails, retain the lease in explicit quarantined/cleanup-pending state and refuse new acquisition until retry/shutdown resolves it; do not publish the invalid interface to consumers. Add a regression that combines invalid ABI validation with one failed release and proves a later cleanup retry removes the retained generation before reacquisition.

- **Consolidation sources:** [automation/bug-scan-20260927-2329](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/d47f3c6d9e5e9d4befae8161565f10f2bf7ac9f2/bugs.md); [Drive: 2026-09-27 23-29 MDT - automation-bug-scan-20260927-2329 - Diff](https://docs.google.com/spreadsheets/d/1UiMQv6PT5i3C5JSql-H4o-mdjzVsqIubpQjmOxFeo1k/edit?usp=drivesdk); [Drive: 2026-09-27 23-29 MDT - automation-bug-scan-20260927-2329 - Instructions](https://docs.google.com/spreadsheets/d/1nMF7-NCdTvVw6nFSxY5Nf9OsjEyDbH7gwpxWc0h8CPI/edit?usp=drivesdk)

### 97. Creating a child path can silently delete an existing regular-file parent

- **Status:** Open.
- **Affected code:** `lib/hal/HalStorage.cpp`, `ensureParentDirUnlocked()`, `HalStorage::ensureDirectoryExists()`, `openFileForWriteUnlocked()`; reachable through `src/native/NativePlatformBridge.cpp::ensureParentDirectory()`, `writeFileAtomic()`, and `writeStreamOpen()`.
- **Trigger / reproduction:** Put a regular file at `/foo`, then ask a native app to write `/sd/foo/bar.txt` with `write_file_atomic()` or `write_stream_open()`. The same behavior is reachable from firmware callers that use `HalStorage::writeFile()` for a child of an existing regular file.
- **Observed / logically demonstrated failure:** Parent-directory preparation treats an existing non-directory as something to repair. `ensureParentDirUnlocked()` logs that the parent is not a directory and calls `sd.remove(parent)`; `HalStorage::ensureDirectoryExists()` does the same before `sd.mkdir()`. The attempted child write can therefore delete the unrelated, previously valid `/foo` file instead of failing because its parent component is not a directory. If directory creation or the later write then fails, the caller sees only a failed write after the original parent file has already been destroyed.
- **Likely root cause:** “Ensure directory” combines directory creation with destructive replacement of a path of the wrong type, violating the normal fail-closed expectation for parent-path validation.
- **Impact:** A typo, corrupt path, or legitimate filename/path collision can destroy unrelated SD data as a side effect of an otherwise unsuccessful save/copy/install operation.
- **Repair direction:** Treat every existing non-directory ancestor as a hard error. Never remove it during parent preparation. Separate explicit replacement semantics from directory creation, propagate the failure through write/open callers, and add regression tests proving `/foo` survives attempts to create `/foo/bar` through both the direct HalStorage and native streamed/atomic write paths.

- **Consolidation sources:** [automation/bug-scan-20260928-0027](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b457dd4b96eeb99bb4cef91c1889fc1ea51fbe50/bugs.md); [automation/bug-scan-20260930-0123](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/be3316ade6780966fd46791ae58d962ccbe0f03f/bugs.md); [Drive: 2026-09-30 01-23 MDT - automation-bug-scan-20260930-0123 - Diff](https://docs.google.com/spreadsheets/d/1_WjxM50XfWlSay0EYe7KKqo0UsDms-ZGMu0yJ7YdWmQ/edit?usp=drivesdk); [Drive: 2026-09-28 00-27 MDT - automation-bug-scan-20260928-0027 - Diff](https://docs.google.com/spreadsheets/d/1dWXVMnMM6va08ID6aExLdecRQBJgyNlRj43AIuOhnyA/edit?usp=drivesdk); [Drive: 2026-09-28 00-27 MDT - automation-bug-scan-20260928-0027 - Instructions](https://docs.google.com/spreadsheets/d/1jLAYo0iauVMdYY9yucY0DZswx2mqCc82H4-2L5RpHO0/edit?usp=drivesdk); [Drive: 2026-09-30 01-23 MDT - automation-bug-scan-20260930-0123 - Instructions](https://docs.google.com/spreadsheets/d/1T-xVFt2LWX3ycdaLCcMfdzuq3AVXOovKHKSKdlD0LGg/edit?usp=drivesdk)

### 98. Syntactically valid but schema-invalid Wi-Fi JSON is accepted as a successful empty credential store

- **Status:** Open.
- **Affected code:** `src/JsonSettingsIO.cpp::loadWifi()`; `src/WifiCredentialStore.cpp::loadFromFile()`.
- **Trigger / reproduction:** Start with one or more credentials already loaded, or leave a valid legacy `/.crosspoint/wifi.bin`, then place syntactically valid JSON such as `{}`, `{"credentials":"not-an-array"}`, or a credentials array containing objects without an `ssid` at `/.crosspoint/wifi.json`. Call `WIFI_STORE.loadFromFile()` or boot through a path that loads the store.
- **Observed / logically demonstrated failure:** `deserializeJson()` succeeds, after which `loadWifi()` immediately replaces `lastConnectedSsid`, clears `store.credentials`, converts `credentials` with `as<JsonArray>()`, and returns `true` without requiring that the array or required credential fields exist. `{}` is therefore reported as a successful load of zero networks, and malformed credential objects can be appended with an empty SSID. Because `WifiCredentialStore::loadFromFile()` returns that success immediately when `wifi.json` exists, the legacy migration path is not attempted either.
- **Likely root cause:** JSON syntax validation is being treated as schema validation, and live store state is mutated before required fields are checked.
- **Impact:** A structurally damaged or hand-edited credential file can silently erase the usable in-memory saved-network list for the session while reporting success, causing automatic networking/OTA to behave as though no credentials exist and masking a still-valid legacy source.
- **Repair direction:** Parse into temporary state, require `credentials` to be an array when present/required, validate non-empty bounded SSIDs and field types, reject malformed entries/store structure, and publish the temporary state only after full validation. On invalid JSON/schema leave the current live store unchanged and follow an explicit recovery/migration policy. Add fixtures for `{}`, wrong-type arrays, missing/empty SSIDs, mixed valid/invalid entries, and a valid legacy file alongside rejected JSON.

- **Consolidation sources:** [automation/bug-scan-20260928-0027](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b457dd4b96eeb99bb4cef91c1889fc1ea51fbe50/bugs.md); [Drive: 2026-09-28 00-27 MDT - automation-bug-scan-20260928-0027 - Diff](https://docs.google.com/spreadsheets/d/1dWXVMnMM6va08ID6aExLdecRQBJgyNlRj43AIuOhnyA/edit?usp=drivesdk); [Drive: 2026-09-28 00-27 MDT - automation-bug-scan-20260928-0027 - Instructions](https://docs.google.com/spreadsheets/d/1jLAYo0iauVMdYY9yucY0DZswx2mqCc82H4-2L5RpHO0/edit?usp=drivesdk)

### 99. Native directory enumeration silently truncates long names and File Browser can act on the wrong path

- **Status:** Open.
- **Affected code:** `src/native/NativeAppHost.cpp::dirNext()`; `lib/NativeApps/include/T5AppApi.h` (`T5_APP_DIRENT_NAME_MAX == 128`); consumers include `Apps/file_browser.c::load_sd_files()`, navigation/open/copy/move/rename/delete path construction.
- **Trigger / reproduction:** On the SD card create a file or directory whose FAT long filename is at least 128 bytes, then browse the containing directory with the native File Browser. For a deterministic alias case, also create a valid shorter entry whose complete name equals the first 127 bytes returned for the long entry.
- **Observed / logically demonstrated failure:** `dirNext()` calls `entry.getName(out->name, sizeof(out->name))` but discards the returned length/status and then forcibly NUL-terminates the 128-byte ABI buffer. Other repository code using `getName()` explicitly rejects `length >= sizeof(name)`, demonstrating that truncation is detectable, but the native directory ABI reports the truncated entry as successful. File Browser stores that clipped name as the selected identity and later reconstructs filesystem paths from it. The long item can therefore fail to open or, when the clipped prefix is also a real entry, an action selected on the displayed long entry can resolve to that different shorter path.
- **Likely root cause:** The fixed-size directory-entry ABI has no explicit truncation state and the bridge ignores the filesystem-reported name length instead of failing closed.
- **Impact:** Long but filesystem-valid names are misrepresented to every native app using directory enumeration. In File Browser this is not merely cosmetic: open, copy, rename, move, and delete operations can be directed at a path other than the enumerated object.
- **Repair direction:** Never publish a clipped name as an object identity. Check `getName()`'s return value, reject/flag oversized entries, and preferably extend directory enumeration with a versioned larger/dynamic name or opaque entry handle so valid FAT LFNs remain addressable. Until then, File Browser should surface an explicit unsupported-name row that cannot perform mutations. Add tests for 127-byte boundary names, 128+ byte names, and a long-name/short-prefix collision proving destructive operations cannot target the wrong entry.

- **Consolidation sources:** [automation/bug-scan-20260928-0027](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b457dd4b96eeb99bb4cef91c1889fc1ea51fbe50/bugs.md); [Drive: 2026-09-28 00-27 MDT - automation-bug-scan-20260928-0027 - Diff](https://docs.google.com/spreadsheets/d/1dWXVMnMM6va08ID6aExLdecRQBJgyNlRj43AIuOhnyA/edit?usp=drivesdk); [Drive: 2026-09-28 00-27 MDT - automation-bug-scan-20260928-0027 - Instructions](https://docs.google.com/spreadsheets/d/1jLAYo0iauVMdYY9yucY0DZswx2mqCc82H4-2L5RpHO0/edit?usp=drivesdk)

### 100. Firmware OTA accepts release-index size/SHA metadata but never verifies the downloaded image against either value

- **Status:** Open.

- **Affected code:** `src/network/OtaUpdater.cpp`, `OtaUpdater::checkForUpdate()` and `OtaUpdater::installUpdate()`; `src/network/OtaUpdater.h`.
- **Trigger / reproduction:** Supply a structurally valid release-index firmware entry whose `url` points to the expected release asset name, but make the served firmware bytes differ from the index's `sha256` and/or `size` while still being an ESP image acceptable to `esp_https_ota`. Start Firmware Update.
- **Observed / logically demonstrated failure:** `checkForUpdate()` requires a 64-character `sha256` field and reads `size`, but it never validates that the digest characters are hexadecimal, does not store the digest in `OtaUpdater`, and only casts `size` into `otaSize`/progress state. `installUpdate()` passes only the URL and TLS configuration to `esp_https_ota`; after transfer it checks the OTA library's completion result but never compares `processedSize` with `otaSize` and never computes or compares the release-index SHA-256. A different self-consistent ESP image at the expected URL can therefore be accepted even though it violates the release catalog's advertised integrity metadata.
- **Likely root cause:** Release-index integrity fields are treated as catalog-shape metadata rather than as constraints on the actual OTA byte stream.
- **Impact:** A stale, mismatched, or incorrectly published firmware asset can be installed despite the release index explicitly identifying a different byte length and SHA-256, weakening release consistency checks and making catalog corruption harder to detect before reboot.
- **Repair direction:** Validate the digest as 64 hexadecimal characters, retain the expected digest and size in `OtaUpdater`, hash/count the actual OTA payload (or use an ESP-IDF verification hook that exposes the image digest), and refuse finalization/boot selection unless both expected values match. Add tests with valid ESP images that intentionally mismatch only size, only SHA-256, and both.

- **Consolidation sources:** [automation/bug-scan-20260928-0123](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/fca75d27f607bae8b6dffa27f64a3115c62b90b8/bugs.md); [automation/bug-scan-20260929-0725](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/7c3008d0876ab9bb0a728aca781b63fbfa9613f3/bugs.md); [automation/bug-scan-20260929-2326](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/95f741bc0a49f92b0cec24836821ba216f48eb5a/bugs.md); [Drive: 2026-09-29 2326 MDT - automation-bug-scan-20260929-2326 - Diff](https://docs.google.com/spreadsheets/d/1AYPw1N2ZOYgHFucbaezdGbIxQMX-0HhylovtsgBIhJc/edit?usp=drivesdk); [Drive: 2026-09-29 0725 MDT - automation-bug-scan-20260929-0725 - instructions](https://docs.google.com/spreadsheets/d/1-4jDQiaUOBHv4-fTEqOZCIfyr8g9jYm1HMFC0fqAc_o/edit?usp=drivesdk); [Drive: 2026-09-28 01-23 MDT - automation-bug-scan-20260928-0123 - Integration Instructions](https://docs.google.com/spreadsheets/d/1EuCL6zffo2jnVhnXVFebqEfDJt-11KbcCYYAT9hwdAc/edit?usp=drivesdk); [Drive: 2026-09-28 01-23 MDT - automation-bug-scan-20260928-0123 - bugs.md diff](https://docs.google.com/spreadsheets/d/1bpAXqZIqxOc7zruLPSLZTZ8vPH6tN1V1RNwBjTUF0B0/edit?usp=drivesdk); [Drive: 2026-09-29 0725 MDT - automation-bug-scan-20260929-0725 - diff](https://docs.google.com/spreadsheets/d/1AJPUdv9PVLhPLWMG1wuU6fzACHMVp59JWvFPEDsjuQ0/edit?usp=drivesdk); [Drive: 2026-09-29 2326 MDT - automation-bug-scan-20260929-2326 - Instructions](https://docs.google.com/spreadsheets/d/1DqYP7D3EugBNzyKRCNek4bVnZX3yy0vbmJllB1R6DBE/edit?usp=drivesdk)

### 101. TXT page-index cache survives same-size edits and can skip or repeat text

- **Status:** Open.
- **Affected code:** `lib/Txt/Txt.cpp`, `Txt::Txt()` and `Txt::load()`; `src/activities/reader/TxtReaderActivity.cpp`, `loadPageIndexCache()`, `savePageIndexCache()`, and navigation through `pageOffsets`.
- **Trigger / reproduction:** Open a TXT file so `index.bin` is built. Rewrite that same pathname with different line breaks or word lengths while preserving exactly the same byte length, then reopen it with the same font/layout settings.
- **Observed / logically demonstrated failure:** The cache directory key is based only on the file path. Cache validation checks the file size and rendering settings but no content fingerprint or file generation. Same-size replacement content therefore reuses byte offsets computed from the old text. If a newly rendered page should consume more bytes than the old cached boundary, text is skipped at the next page; if it should consume fewer, text is repeated.
- **Likely root cause:** File length is treated as sufficient identity for pagination content.
- **Impact:** Editing or replacing a plain-text book in place can silently omit or duplicate text until the cache is rebuilt for some unrelated reason.
- **Repair direction:** Bind the index to content identity using a stable digest or bounded fingerprint plus size, optionally modification metadata where reliable, and rebuild on mismatch. Add a same-length rewrite regression with changed wrapping/page boundaries.

- **Consolidation sources:** [automation/bug-scan-20260928-0123](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/fca75d27f607bae8b6dffa27f64a3115c62b90b8/bugs.md); [Drive: 2026-09-28 01-23 MDT - automation-bug-scan-20260928-0123 - Integration Instructions](https://docs.google.com/spreadsheets/d/1EuCL6zffo2jnVhnXVFebqEfDJt-11KbcCYYAT9hwdAc/edit?usp=drivesdk); [Drive: 2026-09-28 01-23 MDT - automation-bug-scan-20260928-0123 - bugs.md diff](https://docs.google.com/spreadsheets/d/1bpAXqZIqxOc7zruLPSLZTZ8vPH6tN1V1RNwBjTUF0B0/edit?usp=drivesdk)

### 102. Non-byte-aligned XTCH heights can read beyond the grayscale page buffer

- **Status:** Open.
- **Affected code:** `lib/Xtc/Xtc/XtcParser.cpp`, `XtcParser::loadPage()`; `src/activities/reader/XtcReaderActivity.cpp`, `renderPage()`; `lib/Xtc/Xtc.cpp`, `generateCoverBmp()`.
- **Trigger / reproduction:** Open an XTCH/XTH page with a height not divisible by eight, for example 480x801, with otherwise accepted headers and enough bitmap bytes for the parser's current `ceil(width * height / 8) * 2` calculation.
- **Observed / logically demonstrated failure:** The parser accepts arbitrary 16-bit dimensions and allocates each plane as `ceil(width * height / 8)`. Rendering addresses the column-major data with `colBytes = ceil(height / 8)` and `byteOffset = colIndex * colBytes + byteInCol`. For 480x801 the allocated plane is 48,060 bytes, but pixel x=0,y=800 addresses byte 48,479; the second-plane access is 419 bytes beyond the two-plane allocation. The page renderer and cover converter do not bounds-check that access.
- **Likely root cause:** Whole-plane bit packing and per-column byte padding are mixed without enforcing an aligned height or a single consistent layout formula.
- **Impact:** A malformed or non-canonical XTCH file can cause out-of-bounds heap reads, corrupted output, or a device reset during page/cover rendering.
- **Repair direction:** Define and enforce one XTH packing contract in the parser. If columns are byte-padded, size each plane as `width * ceil(height / 8)`; if the format is tightly packed, use continuous bit addressing. Otherwise reject non-8-aligned heights. Share a checked offset helper and add 800/801-height plus truncated-data tests.

- **Consolidation sources:** [automation/bug-scan-20260928-0123](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/fca75d27f607bae8b6dffa27f64a3115c62b90b8/bugs.md); [automation/bug-scan-20260929-1721](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/990fb155e4153f52af78d43ee4a6020fc21d2b9f/bugs.md); [Drive: 2026-09-28 01-23 MDT - automation-bug-scan-20260928-0123 - Integration Instructions](https://docs.google.com/spreadsheets/d/1EuCL6zffo2jnVhnXVFebqEfDJt-11KbcCYYAT9hwdAc/edit?usp=drivesdk); [Drive: 2026-09-28 01-23 MDT - automation-bug-scan-20260928-0123 - bugs.md diff](https://docs.google.com/spreadsheets/d/1bpAXqZIqxOc7zruLPSLZTZ8vPH6tN1V1RNwBjTUF0B0/edit?usp=drivesdk); [Drive: 2026-09-29_1721_MDT_no-PR_automation-bug-scan-20260929-1721_instructions](https://docs.google.com/spreadsheets/d/1B6jsqIdvTzuAJtVj_QpirPvKMi93crRUKinwVqmHxAs/edit?usp=drivesdk); [Drive: 2026-09-29_1721_MDT_no-PR_automation-bug-scan-20260929-1721_diff](https://docs.google.com/spreadsheets/d/1T0ewi9rDs-IC-vdvoFAHlkma1VR2j07_dIVGykhjkjE/edit?usp=drivesdk)

### 103. Partial mandatory-capability binding can retain an installed-provider lease with no reachable cleanup path

- **Status:** Open.
- **Affected code:** `src/native/NativeCapabilityGate.cpp::native_app_capabilities_bind()`, `releaseInstalledBindings()`, `native_app_capabilities_release()`, and the `installedBindings` state.
- **Trigger / reproduction:** Launch an app whose manifest has at least two mandatory capabilities resolved through installed providers. Arrange for the first provider acquisition to succeed, the second mandatory provider acquisition to fail, and the rollback release of the first provider lease to fail once. This can be reproduced with a provider teardown fault injected into `RuntimeInstalledProviders::release()` after a successful first acquisition.
- **Observed / logically demonstrated failure:** `native_app_capabilities_bind()` sets `installedBindings.owner` and stores each acquired lease as it proceeds. If a later acquisition fails, it calls `releaseInstalledBindings(invocation)`; that helper deliberately retains any lease whose release failed. However, the dependency cleanup callback is registered with `ExecutionContext::track(Resource::Dependencies,...)` only after all requirements have been acquired and the live binding table has successfully bound. The failed bind therefore leaves a retained `installedBindings.owner`/lease without a tracked teardown callback. The normal explicit release path is also unreachable: `native_app_capabilities_release()` returns early unless `bindings.owner() == context->id()`, but `bindings` was never published on this failure path. A later bind then sees the previous installed binding still active and refuses to proceed.
- **Likely root cause:** Cleanup ownership is tied to successful publication of the live capability-binding table, even though installed-provider acquisition can create a retryable cleanup obligation before that publication point.
- **Impact:** One transient provider-release failure during a rejected app launch can strand a provider generation/package pin and make later mandatory-capability app launches fail until some unrelated full graph reset or reboot.
- **Repair direction:** Register rollback cleanup before the first installed-provider side effect, or make `native_app_capabilities_release()` independently recognize and retry `installedBindings.owner` even when `bindings.owner()` is unset. Preserve the failed-cleanup state until release actually succeeds, and add a fault-injection test covering first acquisition success + later acquisition failure + first rollback release failure followed by a successful teardown retry.

- **Consolidation sources:** [automation/bug-scan-20260928-0222](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/8291438cdc96d5aa8efc2b053ebf80b1723183ec/bugs.md); [Drive: 2026-09-28 02-22 MDT automation-bug-scan-20260928-0222 Instructions](https://docs.google.com/spreadsheets/d/1h_jKtLcEGDvljyqawgqykKgLTy_r-mBKQry0vZOQo6I/edit?usp=drivesdk); [Drive: 2026-09-28 02-22 MDT automation-bug-scan-20260928-0222 Diff](https://docs.google.com/spreadsheets/d/1bgLz5vfhCQB_4pFDWO9PXwq1QRF-dtA8SJ4LONYnQck/edit?usp=drivesdk); [Drive: 2026-09-28 02-22 MDT automation-bug-scan-20260928-0222 Diff](https://docs.google.com/spreadsheets/d/1-L0v6uFPWVW5o5Z1pBdtxzxlBVJu7UTwCf9NOpBSeEM/edit?usp=drivesdk)

### 104. Display-takeover teardown makes a failed firmware display restore non-retryable

- **Status:** Open.
- **Affected code:** `src/native/NativeHardwareTakeover.cpp::native_hardware_takeover_end()`, specifically the `s_display_borrowed` state transition around `display.resumeFromExternalOwner()`.
- **Trigger / reproduction:** Run an ELF that requests `T5_HARDWARE_TAKEOVER_DISPLAY`, then on app exit force `display.resumeFromExternalOwner()` to fail once (for example with a panel/bus reinitialization fault) while the takeover flag is set.
- **Observed / logically demonstrated failure:** The teardown first calls `nativeVideoForceStop()`, then sets `s_display_borrowed = false`, and only afterward attempts `display.resumeFromExternalOwner()`. If that restore returns false, `native_hardware_takeover_end()` returns `ESP_FAIL`, but the ownership flag already says the display is no longer borrowed. Retrying the same teardown immediately returns `ESP_ERR_INVALID_STATE` because `s_display_borrowed` is false, so the failed display restoration cannot be retried through the ownership protocol. The firmware can therefore continue with its display state marked returned even though reinitialization failed. Open PR #220 modifies the refresh mode after successful restoration but leaves this pre-restore flag clear unchanged.
- **Likely root cause:** The logical ownership transition is committed before the physical display restore is known to have succeeded.
- **Impact:** A transient display/panel restore failure at native-app exit can leave the firmware with no retryable path to reclaim a usable display, potentially producing a blank/unresponsive UI until another full display initialization or reboot.
- **Repair direction:** Keep `s_display_borrowed` (or an explicit restore-pending state) set until `resumeFromExternalOwner()` succeeds, make teardown idempotent/retryable after a failed restore, and only publish firmware ownership after successful panel recovery. Add a test that fails the first resume attempt, verifies ownership remains pending, and succeeds on a second teardown call.

- **Consolidation sources:** [automation/bug-scan-20260928-0222](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/8291438cdc96d5aa8efc2b053ebf80b1723183ec/bugs.md); [Drive: 2026-09-28 02-22 MDT automation-bug-scan-20260928-0222 Instructions](https://docs.google.com/spreadsheets/d/1h_jKtLcEGDvljyqawgqykKgLTy_r-mBKQry0vZOQo6I/edit?usp=drivesdk); [Drive: 2026-09-28 02-22 MDT automation-bug-scan-20260928-0222 Diff](https://docs.google.com/spreadsheets/d/1bgLz5vfhCQB_4pFDWO9PXwq1QRF-dtA8SJ4LONYnQck/edit?usp=drivesdk); [Drive: 2026-09-28 02-22 MDT automation-bug-scan-20260928-0222 Diff](https://docs.google.com/spreadsheets/d/1-L0v6uFPWVW5o5Z1pBdtxzxlBVJu7UTwCf9NOpBSeEM/edit?usp=drivesdk)

### 105. A stale File Browser rename handoff can rename the wrong first directory entry

- **Status:** Open.
- **Affected code:** `Apps/file_browser.c::find_entry()`, `load_files()`, `load_session()`, `request_rename()`, and `consume_handoff_results()`.
- **Trigger / reproduction:** Select a non-first SD entry and choose **Rename**, which saves the selected name before opening the system keyboard. While the keyboard handoff is active, remove or rename that source entry through another filesystem actor (for example the web file service or another host-side operation), then submit a new name and let File Browser resume.
- **Observed / logically demonstrated failure:** `find_entry()` returns index 0 both when the requested saved name is genuinely the first entry and when the name is not found at all. During resume, `load_session()` calls `load_files(saved_name)`; if the original source disappeared, that ambiguous return value selects `entries[0]` and still sets `selection_active = true`. When the pending keyboard result is consumed, `selected_name(old_name,...)` takes the newly selected first entry as the rename source, and the subsequent `storage->rename_file(source_vfs,destination_vfs)` can rename that unrelated object to the user's requested name.
- **Likely root cause:** The selection lookup has no “not found” sentinel, and the asynchronous rename transaction is rebound to the current selected index after resume instead of to the immutable source identity saved when rename began.
- **Impact:** A stale rename prompt can mutate the wrong file or directory rather than merely failing because its original source vanished.
- **Repair direction:** Make `find_entry()` return an explicit negative/not-found result, clear selection when a preserved name is absent, and store/revalidate the exact original source path as part of the rename handoff before applying the keyboard result. Add a regression test where the selected source disappears between `request_rename()` and `consume_handoff_results()` and verify that no other entry is renamed.

- **Consolidation sources:** [automation/bug-scan-20260928-0222](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/8291438cdc96d5aa8efc2b053ebf80b1723183ec/bugs.md); [Drive: 2026-09-28 02-22 MDT automation-bug-scan-20260928-0222 Instructions](https://docs.google.com/spreadsheets/d/1h_jKtLcEGDvljyqawgqykKgLTy_r-mBKQry0vZOQo6I/edit?usp=drivesdk); [Drive: 2026-09-28 02-22 MDT automation-bug-scan-20260928-0222 Diff](https://docs.google.com/spreadsheets/d/1bgLz5vfhCQB_4pFDWO9PXwq1QRF-dtA8SJ4LONYnQck/edit?usp=drivesdk); [Drive: 2026-09-28 02-22 MDT automation-bug-scan-20260928-0222 Diff](https://docs.google.com/spreadsheets/d/1-L0v6uFPWVW5o5Z1pBdtxzxlBVJu7UTwCf9NOpBSeEM/edit?usp=drivesdk)

### 106. Driver Manager cannot expose more than 64 release, inbox, or recovery entries

- **Status:** Open.

- **Affected code:** `Apps/driver_manager.c`: `LIMIT`, `RECOVERY_LIMIT`, `populate_release()`, `load_inbox()`, and `recovery_screen()`; `src/native/NativeDriverManagerBridge.cpp`: `kMaxDriverAssets`, `loadCanonicalDriverCatalog()`, legacy catalog loading, and recovery inventory. Open PR #96 rewrites Driver Manager but retains `LIMIT 64u` and does not remove this bound.
- **Trigger / reproduction:** Publish or otherwise present more than 64 valid driver packages in the current release catalog, place more than 64 valid driver-package directories in `/sd/Packages/Inbox`, or retain more than 65 recoverable driver stages/downloads. Open Driver Manager and attempt to reach an entry beyond the corresponding limit.
- **Observed / logically demonstrated failure:** The app clamps the online catalog to `LIMIT`, stops materializing inbox rows once `row_count >= LIMIT`, and clamps recovery rows to `RECOVERY_LIMIT`; there is no paging, cursor, or truncation notice. The firmware bridge also rejects a canonical driver index whose `drivers` array exceeds `kMaxDriverAssets == 64`, while legacy discovery stops adding entries at that limit. Therefore valid drivers beyond the fixed workspace are either hidden from the UI or can make the authoritative catalog path fail and fall back to an incomplete bounded view.
- **Likely root cause:** Driver discovery and presentation share fixed-size workspaces that were treated as service limits instead of page sizes.
- **Impact:** As the driver ecosystem grows, valid drivers can become impossible to install/update through Driver Manager; retained recovery work can also become unreachable, making package recovery dependent on directory/catalog ordering.
- **Repair direction:** Make the bridge catalog/recovery model dynamic or PSRAM-backed and expose the complete count; page/virtualize Driver Manager rows over that model. If bounded parsing is still required, reject only an individual invalid entry rather than the whole canonical catalog and surface explicit overflow state. Add coverage with 65+ online drivers, 65+ inbox packages, and 66+ recovery items, including an actionable item beyond the first page.

- **Consolidation sources:** [automation/bug-scan-20260928-0327](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/5dc17fa9909c50a6cea69d852f0664b099d5df4d/bugs.md); [automation/bug-scan-20260928-1623](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/0b0cd92cfb5cafc7d4b5baa897442b2acb9b1030/bugs.md); [Drive: 2026-09-28 16-23 MDT - automation_bug-scan-20260928-1623 - Instructions](https://docs.google.com/spreadsheets/d/1j8Lm6ObGsHFSC50yu0IP8iFt5c14EkbJsHcCGh9edpI/edit?usp=drivesdk); [Drive: 2026-09-28 0327 MDT - automation_bug-scan-20260928-0327 - Instructions](https://docs.google.com/spreadsheets/d/1mlsbKN6llrbvLhsLOTizXaIvqWo6bFdTsuk0PlUEQ14/edit?usp=drivesdk); [Drive: 2026-09-28 0327 MDT - automation_bug-scan-20260928-0327 - Diff](https://docs.google.com/spreadsheets/d/1pBADylIFdjuKM_vqjiZxLWFskc-qxQxjA_IF93xv3rw/edit?usp=drivesdk); [Drive: 2026-09-28 16-23 MDT - automation_bug-scan-20260928-1623 - Diff](https://docs.google.com/spreadsheets/d/1j_TEfpX-sHjsgfaRCxGHsuRPpo8pn12zPHTNYv1jGkw/edit?usp=drivesdk)

### 107. The serviced-refresh worker permanently reserves 32 KiB for an intended 8 KiB task stack

- **Status:** Open.
- **Affected:** `src/native/NativeAppHost.cpp`: `refreshStack`, `presentServicedMode()`, `xTaskCreateStaticPinnedToCore(renderServicedFrame, ...)`.
- **Trigger / reproduction:** Build the ESP32-S3 firmware and inspect the static `refreshStack` allocation or internal-RAM map. `StackType_t` is 32 bits on this target, while the array is declared as `StackType_t refreshStack[8192]`.
- **Failure:** The source comment says the renderer worker reserves an 8 KiB stack, but the backing array consumes 8192 * 4 = 32768 bytes. ESP-IDF task-creation stack depths are specified in bytes, and the call passes `sizeof(refreshStack)`, so the task is actually provisioned with the full 32 KiB buffer rather than 8 KiB.
- **Root cause / impact:** The desired byte count was used as a count of `StackType_t` elements. This permanently consumes about 24 KiB more internal DRAM than intended, reducing contiguous heap available to TLS, driver loading, USB, display, and other memory-sensitive firmware paths.
- **Repair direction:** Size the static buffer in stack words from a byte constant (for example `kRefreshStackBytes / sizeof(StackType_t)`) and pass the byte count expected by ESP-IDF. Confirm the worker's real high-water mark on device before selecting the final stack size, and add a compile-time assertion that the buffer byte size equals the configured byte budget.

- **Consolidation sources:** [automation/bug-scan-20260928-0327](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/5dc17fa9909c50a6cea69d852f0664b099d5df4d/bugs.md); [Drive: 2026-09-28 0327 MDT - automation_bug-scan-20260928-0327 - Instructions](https://docs.google.com/spreadsheets/d/1mlsbKN6llrbvLhsLOTizXaIvqWo6bFdTsuk0PlUEQ14/edit?usp=drivesdk); [Drive: 2026-09-28 0327 MDT - automation_bug-scan-20260928-0327 - Diff](https://docs.google.com/spreadsheets/d/1pBADylIFdjuKM_vqjiZxLWFskc-qxQxjA_IF93xv3rw/edit?usp=drivesdk)

### 108. The native image API still exposes the PNG decode path that the bundled Image Viewer explicitly avoids because it can crash

- **Status:** Open.
- **Affected:** `src/native/NativeImageBridge.cpp`: `renderFit()`, `pngDraw()`; `Apps/image_viewer.c`: PNG guard in `app_main()`.
- **Trigger / reproduction:** From any native app other than the bundled Image Viewer, obtain `t5_image_api_v1` and call `render_fit()` on a real-world PNG that triggers the known PNGdec fault documented in `Apps/image_viewer.c`.
- **Failure:** Image Viewer detects PNG and deliberately refuses to call `render_fit()`, displaying “PNG decoder error prevented” because the firmware path can fault inside PNGdec. The shared `t5_image_api_v1::render_fit` implementation nevertheless continues to enter that same PNGdec path for every other native app, so the safety workaround is not enforced at the API boundary and another consumer can still crash the firmware.
- **Root cause / impact:** A known decoder defect is worked around in one application instead of being fixed or quarantined in the shared image bridge that owns the unsafe decoder. The API therefore advertises PNG rendering as available even though it is not safe for all valid inputs.
- **Repair direction:** Fix and regression-test the PNGdec integration in `NativeImageBridge` (including the real-world files that originally faulted). Until that is safe, make the bridge reject PNG `render_fit()` calls consistently rather than relying on each consumer to know about the crash. Remove the app-local guard only after the shared decoder path is proven safe.

- **Consolidation sources:** [automation/bug-scan-20260928-0327](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/5dc17fa9909c50a6cea69d852f0664b099d5df4d/bugs.md); [Drive: 2026-09-28 0327 MDT - automation_bug-scan-20260928-0327 - Instructions](https://docs.google.com/spreadsheets/d/1mlsbKN6llrbvLhsLOTizXaIvqWo6bFdTsuk0PlUEQ14/edit?usp=drivesdk); [Drive: 2026-09-28 0327 MDT - automation_bug-scan-20260928-0327 - Diff](https://docs.google.com/spreadsheets/d/1pBADylIFdjuKM_vqjiZxLWFskc-qxQxjA_IF93xv3rw/edit?usp=drivesdk)

### 109. Time Card treats stores over 400 days as successfully loaded, then can erase the omitted history

- **Status:** Open.
- **Affected code:** `Apps/timecard.c`, `MAX_DAYS`, `load_store()`, `ensure_day()`, `set_punch()`, and `save_store()`.
- **Trigger / reproduction:** Put more than 400 valid day objects in `/sd/.crosspoint/timecard.json` (or let the file naturally grow beyond that count), launch Time Card, then record or edit any punch.
- **Observed / logically demonstrated failure:** `load_store()` stops its parse loop as soon as `day_count == MAX_DAYS`, but it does not treat the remaining unparsed day objects as an error and returns `true`. The live store therefore contains only the first 400 valid days. A later `set_punch()` calls `save_store()`, which rewrites the JSON from that truncated in-memory array and permanently drops every valid day after entry 400.
- **Likely root cause:** The fixed-capacity in-memory limit is used as an implicit parser terminator instead of a validated storage constraint, and a capacity hit is reported as a successful complete load.
- **Impact:** Long-running Time Card histories can be silently truncated and then irreversibly rewritten merely by making another punch, even though the source JSON was valid.
- **Repair direction:** Detect capacity exhaustion before declaring the load successful. Prefer a bounded retention policy that is explicit and preserves the intended newest/oldest records, or migrate to dynamically sized storage. Never allow a partial load to become writable state without an explicit recovery decision. Add a regression with 401+ valid days proving the source file is not rewritten with records omitted.

- **Consolidation sources:** [automation/bug-scan-20260928-0426](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/079fd38ed3a26fbdb2d4031a8712d346071bbaf9/bugs.md); [Drive: 2026-09-28 04-26 MDT - automation-bug-scan-20260928-0426 - integration instructions](https://docs.google.com/spreadsheets/d/1va_Vt6BhUcAVkXQjsmGdO91OcaUxcKmC5Mlk_ekDtTs/edit?usp=drivesdk); [Drive: 2026-09-28 04-26 MDT - automation-bug-scan-20260928-0426 - bugs.md diff](https://docs.google.com/spreadsheets/d/1eE-l3V29VpfE1vM_mDFQEH_ih3r8DQFbvuG4YCk-Uw0/edit?usp=drivesdk)

### 110. TXT BMP-cover caching reports success after short reads or writes and permanently accepts the partial cache

- **Status:** Open.
- **Affected code:** `lib/Txt/Txt.cpp`, `Txt::generateCoverBmp()` BMP-copy path and the existing-file fast path.
- **Trigger / reproduction:** Place a valid BMP sidecar cover next to a TXT/Markdown book, then inject an SD read or cache-write failure after at least one chunk while `generateCoverBmp()` copies the cover into the TXT cache.
- **Observed / logically demonstrated failure:** The BMP branch loops over `src.read()` and calls `dst.write()` but checks neither for a zero/short read nor a short/failed write. It then unconditionally logs success and returns `true`. The incomplete `cover.bmp` remains in the cache. On every later call, the first `Storage.exists(getCoverBmpPath())` check returns `true` without validating the cached bitmap, so the truncated file is treated as a permanently successful cover generation.
- **Likely root cause:** The direct BMP-copy path lacks transactional publication and I/O result validation, while cache validity is defined only as path existence.
- **Impact:** A transient SD fault can poison a book's cover cache across later launches, causing missing/corrupt artwork until the cache is manually removed or rebuilt by some unrelated action.
- **Repair direction:** Copy to a temporary cache file, require every read/write to make full forward progress, close successfully, validate the completed BMP at least structurally, and atomically publish it only after success. Remove the temporary file on any failure. The existing-file fast path should reject obviously incomplete/invalid cached BMPs. Add injected short-read and short-write tests followed by a retry proving recovery occurs automatically.

- **Consolidation sources:** [automation/bug-scan-20260928-0426](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/079fd38ed3a26fbdb2d4031a8712d346071bbaf9/bugs.md); [Drive: 2026-09-28 04-26 MDT - automation-bug-scan-20260928-0426 - integration instructions](https://docs.google.com/spreadsheets/d/1va_Vt6BhUcAVkXQjsmGdO91OcaUxcKmC5Mlk_ekDtTs/edit?usp=drivesdk); [Drive: 2026-09-28 04-26 MDT - automation-bug-scan-20260928-0426 - bugs.md diff](https://docs.google.com/spreadsheets/d/1eE-l3V29VpfE1vM_mDFQEH_ih3r8DQFbvuG4YCk-Uw0/edit?usp=drivesdk)

### 111. File Browser silently truncates deep paths and can apply destructive actions to a different pathname

- **Status:** Open.
- **Affected code:** `Apps/file_browser.c`, `PATH_CAP`, `copy_text()`, `join_relative_path()`, `make_storage_path()`, `make_sd_vfs_dir()`, `make_usb_path()`, navigation into child directories, and rename/move/delete/copy actions.
- **Trigger / reproduction:** Create a valid nested SD path whose directory path plus selected entry name exceeds 511 bytes, navigate into it with File Browser, then invoke Rename, Move, Delete, or Copy. A deterministic high-risk case is one where the 511-byte truncated prefix is itself a valid existing path.
- **Observed / logically demonstrated failure:** All path builders use `copy_text()`, which silently truncates when the destination buffer fills and provides no success/failure signal. `base_path` and pending action paths are fixed at `PATH_CAP == 512`. The app therefore cannot distinguish the intended full pathname from its clipped prefix. Destructive operations subsequently pass the truncated path to storage APIs; if that prefix names a real object, the action can target the wrong file or directory rather than merely failing.
- **Likely root cause:** Fixed-size pathname buffers are combined with truncating string assembly instead of checked construction that fails when the complete path cannot be represented.
- **Impact:** Deep but filesystem-valid directory trees can become inaccessible, and rename/move/delete/copy can operate on an unintended prefix path. This is especially severe for Delete and Move because the confirmation UI still describes the originally selected entry name.
- **Repair direction:** Make every path-construction helper return success only when the complete path fits, propagate failure before any filesystem mutation, and keep the UI-selected identity separate from a bounded display string. Prefer the platform's canonical maximum path type/size rather than a private 512-byte limit. Add boundary tests at 510/511/512+ bytes and a regression where the truncated prefix exists, proving no mutation occurs.

- **Consolidation sources:** [automation/bug-scan-20260928-0426](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/079fd38ed3a26fbdb2d4031a8712d346071bbaf9/bugs.md); [Drive: 2026-09-28 04-26 MDT - automation-bug-scan-20260928-0426 - integration instructions](https://docs.google.com/spreadsheets/d/1va_Vt6BhUcAVkXQjsmGdO91OcaUxcKmC5Mlk_ekDtTs/edit?usp=drivesdk); [Drive: 2026-09-28 04-26 MDT - automation-bug-scan-20260928-0426 - bugs.md diff](https://docs.google.com/spreadsheets/d/1eE-l3V29VpfE1vM_mDFQEH_ih3r8DQFbvuG4YCk-Uw0/edit?usp=drivesdk)

### 112. Rom Manager silently makes ROMs after the first 128 unmanageable

- **Status:** Open.
- **Affected code:** `Apps/rom_manager.c`: `MAX_ROMS`, `rom_names` / `rom_sizes`, `load_roms()`, and the `VIEW_ROMS` rename/delete action flow.
- **Trigger / reproduction:** Put more than 128 valid `.gb` files in `/System/State/Applications/Rom Manager`, open Rom Manager, choose **My ROMs**, and try to locate/manage every file.
- **Observed / logically demonstrated failure:** `load_roms()` enumerates only while `rom_count < MAX_ROMS` with `MAX_ROMS == 128`, then closes the directory without indicating truncation or continuing enumeration. The My ROMs UI and its Rename/Delete actions are driven exclusively by those cached arrays, so every valid ROM beyond the first 128 directory entries is absent and cannot be managed from the app. Directory iteration order also makes which ROMs disappear filesystem-dependent.
- **Likely root cause:** A fixed-size presentation cache is used as an enumeration limit rather than as a page/window over the complete directory.
- **Impact:** Larger ROM libraries become only partially manageable; users cannot discover, rename, or delete the omitted files through Rom Manager and receive no warning that the library was truncated.
- **Repair direction:** Page/stream directory results or maintain a dynamically allocated/PSRAM-backed index, and distinguish end-of-directory from capacity exhaustion. Surface enumeration failure/truncation rather than silently treating it as a complete list. Add a regression with at least 129 ROMs and verify all entries remain reachable across pages/windows.

- **Consolidation sources:** [automation/bug-scan-20260928-0518](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/a90cbab88e37ceee8eb4b37eceba4d93553fb2de/bugs.md); [automation/bug-scan-20260928-1825](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/d704fa1cea722332c6d82f0aa2d3f9680ac8ae87/bugs.md); [Drive: 2026-09-28 05-18 MDT - automation-bug-scan-20260928-0518 - Instructions](https://docs.google.com/spreadsheets/d/1rCpCc1hG4ZhCTsjPSfGWl7UOdP6JOEQ2NfbOUGOL_1I/edit?usp=drivesdk); [Drive: 2026-09-28 05-18 MDT - automation-bug-scan-20260928-0518 - Diff](https://docs.google.com/spreadsheets/d/1WspiF_LH3fLG2UlbuLAFMUSAU-RD62l1EDg2_ZatVFU/edit?usp=drivesdk); [Drive: 2026-09-28 1825 - automation-bug-scan-20260928-1825 - bugs.diff](https://docs.google.com/document/d/13Ixd0HQYso07yZjpKF_kGSw0XRsQVXTqpSpQPaI3JE8/edit?usp=drivesdk); [Drive: 2026-09-28 1825 - automation-bug-scan-20260928-1825 - integration instructions](https://docs.google.com/document/d/1KM1MaWJm_OiNONX7BqGauWCR3EJYlmu9TB6Xi1bzpuc/edit?usp=drivesdk)

### 113. Fallback OTA release metadata can accept a truncated JSON document after the required fields were seen

- **Status:** Open.
- **Affected code:** `lib/JsonParser/StreamingJsonParser.cpp` / `.h`, `lib/JsonParser/ReleaseJsonParser.cpp` / `.h`, and the GitHub-release fallback in `src/network/OtaUpdater.cpp::checkForUpdate()`.
- **Trigger / reproduction:** Make the independent firmware release-index request fail so `checkForUpdate()` falls back to the GitHub latest-release JSON endpoint. Feed a response whose `tag_name` and matching firmware asset object are complete but whose outer release object is truncated at EOF, for example a document ending immediately after the `assets` array's closing bracket and omitting the final top-level `}`.
- **Observed / logically demonstrated failure:** `StreamingJsonParser` has no end-of-document/finalization check, and `ReleaseJsonParser` exposes only whether the tag and firmware fields were observed. Once the matching asset object closes, `foundFirmware()` is already true. `OtaUpdater::checkForUpdate()` tests only `foundTag()` and `foundFirmware()`, so the truncated, syntactically incomplete response is accepted as valid release metadata and can be offered for installation.
- **Likely root cause:** The streaming parser validates tokens while bytes arrive but never proves that EOF occurred in a completed root value with balanced containers; the OTA fallback treats field discovery as equivalent to successful document parsing.
- **Impact:** A dropped/corrupt fallback metadata response can publish an update candidate whose containing release document was never completely received or validated. The existing truncated-input tests check only that parsing does not crash, not that an incomplete document cannot be accepted by the OTA caller.
- **Repair direction:** Add an explicit `finish()`/completion result to `StreamingJsonParser` that rejects unfinished tokens, parser errors, and unbalanced/incomplete containers; expose that status through `ReleaseJsonParser`; and require successful completion in `OtaUpdater::checkForUpdate()` before consuming discovered fields. Add a regression with a fully formed firmware asset followed by EOF before the root close.

- **Consolidation sources:** [automation/bug-scan-20260928-0632](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/3b7ba1d3ab884ce95f02f835e265d030ef4b7724/bugs.md); [Drive: 2026-09-28 06-32 MDT - automation_bug-scan-20260928-0632 - Instructions](https://docs.google.com/spreadsheets/d/1fbEaBPIpMH09Xciai_xlzuhH1dKzdc42tHYzqkvUWRY/edit?usp=drivesdk); [Drive: 2026-09-28 06-32 MDT - automation_bug-scan-20260928-0632 - Diff](https://docs.google.com/spreadsheets/d/1_0FeXHnYjexRF4z3B_lzH1R5EsiOeivS4kjm5hjWsvE/edit?usp=drivesdk)

### 114. Serial provider release failure discards the only retryable lease state and permits removal of a still-live provider

- **Status:** Open.
- **Affected code:** `src/runtime/capabilities/SerialProviderRegistry.h`, `RuntimeSerial::Registry::release()`, `end()`, and `remove()`; existing coverage in `test/streams/serial_provider_registry_test.cpp`.
- **Trigger / reproduction:** Register a serial provider whose `acquire()` succeeds but whose `release()` returns an error such as `T5_SERIAL_IO` on the first teardown attempt. Acquire a port and call `Registry::release(publicLease)`, then retry the release or remove the provider.
- **Observed / logically demonstrated failure:** `Registry::release()` copies the provider/private lease, then clears `active_`, `publicLease_`, and `privateLease_` *before* invoking the provider's fallible `release()`. If that callback fails, the error is returned but all registry state needed to retry has already been destroyed. A second release reports `T5_SERIAL_CLOSED`, and `remove(id)` can now succeed because `active_` is null even though the provider may still own its underlying transport/session. This violates the registry's stated invariant that a provider may not be removed with an outstanding lease.
- **Likely root cause:** Public-handle invalidation was made unconditional before provider teardown instead of committing the state transition only after teardown succeeds, or retaining a distinct cleanup-pending state.
- **Impact:** A transient close failure can leak a serial transport/session until reboot and can allow the provider record (and potentially its backing module/context) to be removed while that resource is still live, making clean retry or recovery impossible.
- **Repair direction:** Keep the provider and private lease reachable until `release()` succeeds, while preventing normal I/O during cleanup; alternatively retain an explicit cleanup-pending state that only permits teardown retries. `end()` must preserve/retry the same obligation. Extend `serial_provider_registry_test.cpp` with a provider that fails release once, verify removal remains blocked, and verify a later release can complete.

- **Consolidation sources:** [automation/bug-scan-20260928-0632](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/3b7ba1d3ab884ce95f02f835e265d030ef4b7724/bugs.md); [Drive: 2026-09-28 06-32 MDT - automation_bug-scan-20260928-0632 - Instructions](https://docs.google.com/spreadsheets/d/1fbEaBPIpMH09Xciai_xlzuhH1dKzdc42tHYzqkvUWRY/edit?usp=drivesdk); [Drive: 2026-09-28 06-32 MDT - automation_bug-scan-20260928-0632 - Diff](https://docs.google.com/spreadsheets/d/1_0FeXHnYjexRF4z3B_lzH1R5EsiOeivS4kjm5hjWsvE/edit?usp=drivesdk)

### 115. Bookmark summary truncation can persist invalid UTF-8 for ordinary non-ASCII page text

- **Status:** Open.
- **Affected code:** `src/util/BookmarkUtil.cpp::sanitizeBookmarkSummary()`; caller `src/activities/reader/EpubReaderActivity.cpp::addBookmark()`.
- **Trigger / reproduction:** Add a bookmark on a page whose extracted summary exceeds 72 bytes and has a multibyte UTF-8 character crossing byte 72, for example 71 ASCII bytes followed by `é`. Non-ASCII whitespace/text also exercises the preceding whitespace-collapse comparator.
- **Observed / logically demonstrated failure:** `sanitizeBookmarkSummary()` truncates with `summary.resize(72)`, which is byte-based and can cut a UTF-8 code point in half. The example keeps only the first byte of the two-byte `é`, and that invalid byte sequence is then stored in the bookmark JSON and later rendered as the bookmark title. The `std::unique` whitespace comparator also passes plain `char` values directly to `std::isspace`; for negative signed-char UTF-8 bytes that call has undefined behavior.
- **Likely root cause:** Bookmark summaries are treated as single-byte text even though reader content and the rest of the UI are UTF-8.
- **Impact:** Normal books containing accented or non-Latin text can create corrupt bookmark summaries, causing replacement glyphs or parser/rendering problems when the saved bookmark is reopened; the ctype misuse also makes non-ASCII handling undefined on signed-char builds.
- **Repair direction:** Collapse whitespace using unsigned-byte-safe classification (or code-point-aware Unicode whitespace handling) and truncate at a valid UTF-8 code-point boundary, preferably by character/display width rather than raw bytes. Add tests with 2-, 3-, and 4-byte characters crossing the limit and with non-ASCII text adjacent to whitespace.

- **Consolidation sources:** [automation/bug-scan-20260928-0632](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/3b7ba1d3ab884ce95f02f835e265d030ef4b7724/bugs.md); [Drive: 2026-09-28 06-32 MDT - automation_bug-scan-20260928-0632 - Instructions](https://docs.google.com/spreadsheets/d/1fbEaBPIpMH09Xciai_xlzuhH1dKzdc42tHYzqkvUWRY/edit?usp=drivesdk); [Drive: 2026-09-28 06-32 MDT - automation_bug-scan-20260928-0632 - Diff](https://docs.google.com/spreadsheets/d/1_0FeXHnYjexRF4z3B_lzH1R5EsiOeivS4kjm5hjWsvE/edit?usp=drivesdk)

### 116. A transient LoRa receive re-arm failure permanently disables reception for the running session

- **Status:** Open.
- **Affected code:** `src/native/NativeLoRaBridge.cpp`, especially `startReceiver()`, `pollPacket()`, and the post-transmit receive restart in `sendPacket()`.
- **Trigger / reproduction:** Start LoRa normally, then force a receive-path error (for example an invalid reported packet length or a one-shot `radio.readData()` failure) and make the immediately following `radio.startReceive()` call fail once. The same terminal state can be reached when a transmit succeeds but the one post-transmit `startReceiver()` attempt fails.
- **Observed / logically demonstrated failure:** `startReceiver()` clears `receiverActive` when `radio.startReceive()` fails. Its callers in the receive-error and post-transmit paths discard that return value. Every later `pollPacket()` begins by returning immediately when `receiverActive` is false, so it never attempts to arm reception again. A transient single re-arm error therefore becomes permanent for that bridge session even after the radio is healthy again. In the transmit case, `sendPacket()` can report success while reception has silently been lost.
- **Likely root cause:** Receive recovery is implemented as a one-shot edge action, while `receiverActive == false` is also used as a guard that prevents the polling path from performing recovery.
- **Impact:** An otherwise healthy radio can stop receiving all subsequent packets until a higher-level stop/start or reboot, breaking long-running LoRa telemetry after one transient radio error.
- **Repair direction:** Track a retryable `needsReceiveArm` state separately from “receiver is currently armed”; while the bridge is running, retry `startReceive()` from the poll/service path with bounded backoff. Do not silently report a fully restored transmit operation if RX re-arm failed. Add fault-injection tests for one failed re-arm after both a receive error and a successful transmit, followed by recovery on the next service cycle.

- **Consolidation sources:** [automation/bug-scan-20260928-0704](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/5429ff14b8803c208a0b7c580567600de8349ddb/bugs.md)

### 117. Ask can submit the same non-idempotent LLM POST twice after an ambiguous transport failure

- **Status:** Open.
- **Affected code:** `Apps/llm_ask.c::complete_question()`; transport semantics in `src/native/NativeNetworkBridge.cpp::httpRequest()`.
- **Trigger / reproduction:** Submit a question and let the HTTPS POST reach the LLM server, then fail the client transport while receiving the response (for example close the connection mid-response or time out after the request body has already been sent). `http_request()` returns false for transport/read failures even though the server may already have processed the POST.
- **Observed / logically demonstrated failure:** `complete_question()` unconditionally performs the identical POST a second time whenever the first `http_request()` returns false. A false transport result is not proof that the request was never delivered; `NativeNetworkBridge` can return false after the POST has been transmitted and response handling fails. The server can therefore execute two completions for one user question, while the app records only whichever response is successfully received.
- **Likely root cause:** The retry policy treats an ambiguous POST transport failure as if it were a pre-send failure. POST completion requests are not inherently idempotent and no request/idempotency key is supplied.
- **Impact:** A single question can consume duplicate remote inference work and make server-side request history disagree with the one response shown and stored by the device.
- **Repair direction:** Do not blindly retry an ambiguous POST. Either expose enough transport phase information to retry only failures proven to occur before request transmission, use a provider-supported idempotency/request key, or require an explicit user retry after an ambiguous failure. Add a test server that accepts the first POST and then drops the response connection, and assert that the client does not automatically submit a second request.

- **Consolidation sources:** [automation/bug-scan-20260928-0704](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/5429ff14b8803c208a0b7c580567600de8349ddb/bugs.md); [automation/bug-scan-20260928-1520](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/2d13c5df568d1a5cb37e4c0aa785e7c6feb861c4/bugs.md); [Drive: 2026-09-28 15-20 MDT - automation-bug-scan-20260928-1520 - Instructions](https://docs.google.com/spreadsheets/d/1wYRFUPOyFqzGet9bQ8tRNpDaAQDcRRdr8DI3vts2vMk/edit?usp=drivesdk); [Drive: 2026-09-28 15-20 MDT - automation-bug-scan-20260928-1520 - Diff](https://docs.google.com/spreadsheets/d/17mygQzs_2CezszJBGTo2MCfbpOCUlNDblXwdtoHkOZE/edit?usp=drivesdk)

### 118. Valid app manifests can declare more provider capabilities than the runtime lease bridge can acquire

- **Status:** Open.
- **Affected code:** `src/native/NativeProviderCapabilityBridge.cpp` (`kMaxActiveLeases = 4`, `active[]`, and `acquire()`); `src/native/AppManifest.cpp` capability-list validation; `src/runtime/capabilities/AppCapabilityRequirements.h` (`kMaxAppRequirements = 6`); `lib/NativeApps/include/T5ProviderCapabilityApi.h`.
- **Trigger / reproduction:** Install a valid native app whose sidecar declares at least five distinct provider capabilities and have the app acquire those interfaces without releasing the earlier leases. Manifest validation permits up to six `requires` entries and six `optional` entries, with duplicates between the lists rejected, while the public provider-capability ABI documents no four-lease limit.
- **Observed / logically demonstrated failure:** The first four acquisitions can occupy every slot in `active[4]`; the fifth call fails with `Capability lease table full` even though the capability was validly declared and an installed compatible provider may be available. Mandatory launch gating can therefore accept a manifest that the runtime interface-acquisition layer cannot actually service. Open PR #96 changes failed-release retention in this bridge but leaves the four-slot limit unchanged.
- **Likely root cause:** The generic lease bridge has an independent hard-coded capacity that is smaller than the manifest capability model it is intended to expose.
- **Impact:** As apps compose more installable input/display/USB/platform providers, otherwise-valid multi-capability apps can fail only at runtime and the failure depends on acquisition order rather than manifest validity or provider availability.
- **Repair direction:** Use a per-invocation lease table sized to the validated capability contract (or dynamically allocate bounded entries), and make any true lease limit explicit and consistent with manifest validation. Add coverage that acquires at least five declared capabilities simultaneously, plus a maximum-contract case spanning the accepted required/optional declaration counts.

- **Consolidation sources:** [automation/bug-scan-20260928-0704](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/5429ff14b8803c208a0b7c580567600de8349ddb/bugs.md)

### 119. Opening or rescanning Wi-Fi Networks tears down an already-working station connection

- **Status:** Open.
- **Affected code:** `src/activities/network/WifiSelectionActivity.cpp`, `onEnter()`, `startWifiScanAttempt()`, and `attemptConnection()`; `src/providers/network/Esp32NetworkProvider.cpp`, `startScan()` and `connect()`; native handoff through `src/native/NativeSystemUiBridge.cpp::NativeWifiActivity::onEnter()`.
- **Trigger / reproduction:** Connect the device to Wi-Fi, then open **Wi-Fi Networks** from a native app and cancel without intentionally changing networks. Test both with a saved `lastConnectedSsid` and with no usable saved last-network credential. Rescanning the list is another deterministic trigger.
- **Observed / logically demonstrated failure:** The selector never preserves or checks the pre-existing station connection. With a saved last network, `onEnter()` immediately calls `attemptConnection()`; provider `connect()` executes `WiFi.disconnect(false, true)` before beginning a new association. Without that auto-connect path, scanning calls `startScan()`, which explicitly switches Wi-Fi to `WIFI_OFF`, brings STA back up, and disconnects again before scanning. Thus merely entering/rescanning the picker interrupts a connection that was already healthy, even though `onExit()` explicitly says the picker should not disconnect Wi-Fi.
- **Likely root cause:** The selection workflow assumes it owns station lifecycle and uses destructive STA reset/scan primitives without distinguishing a pre-existing shared connection from a connection it created.
- **Impact:** Opening or cancelling the Wi-Fi selector can break networking that another workflow was already using, cause avoidable reconnect latency, and return a disconnected result to the requesting native app even though the user did not choose to disconnect.
- **Repair direction:** Snapshot connection ownership/state on entry. If already connected, do not auto-reconnect to `lastConnectedSsid`; use a non-disruptive scan path where supported, or defer destructive station reset until the user explicitly chooses another network. On cancel, preserve/restore the pre-existing connection. Add tests proving open/cancel and rescan do not call the destructive disconnect path for an already-connected station.

- **Consolidation sources:** [automation/bug-scan-20260928-0738](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/ae95ab57b54b4745074a22227bad25c5341ba4b2/bugs.md); [Drive: 2026-09-28 07-38 MDT - automation-bug-scan-20260928-0738 - Instructions](https://docs.google.com/spreadsheets/d/1X2Pqr2AHuLtG36KzrvkQEZ5LRxKnGR9yO4riBi6bm1c/edit?usp=drivesdk); [Drive: 2026-09-28 07-38 MDT - automation-bug-scan-20260928-0738 - Diff](https://docs.google.com/spreadsheets/d/17ntuX4WGxnYrfhLIbPor3h_0wbt-EkjvMrvPg-9TLWA/edit?usp=drivesdk)

### 120. Replacing an EPUB at the same path reuses the previous book's cache

- **Status:** Open.

- **Affected code:** `lib/Epub/Epub.h::Epub()`, `lib/Epub/Epub.cpp::load()`, and `lib/Epub/Epub/BookMetadataCache.cpp::load()`; dependent cached metadata, spine/TOC, CSS, sections, covers, and thumbnails live under the same pathname-derived cache directory.
- **Trigger / reproduction:** Open an EPUB once so its cache is built, then replace that EPUB file in place with a different valid book using the same pathname and open it again without manually clearing the reader cache.
- **Observed / logically demonstrated failure:** The `Epub` constructor derives `cachePath` only from `std::hash<std::string>{}(filepath)`. `load()` immediately trusts an existing version-compatible `book.bin`, and `BookMetadataCache::load()` contains no source-file size, timestamp, digest, ZIP identity, or other generation check. The replacement therefore inherits the old title/author, spine and TOC offsets, CSS/section state, and cover caches. Subsequent chapter lookups can address paths that existed only in the previous archive, so the UI can show stale metadata and navigation or fail to open content from the replacement.
- **Likely root cause:** Cache identity is bound to the storage pathname rather than to the EPUB generation stored at that pathname.
- **Impact:** Updating, re-downloading, or replacing a book without changing its filename can silently present data from a different book and leave reading/navigation broken until the cache is manually removed.
- **Repair direction:** Persist a source identity with the cache and validate it before accepting `book.bin`—prefer a robust archive fingerprint, or at minimum a generation tuple that changes on replacement. On mismatch, invalidate the complete EPUB cache tree before rebuilding so metadata, spine/TOC, CSS, sections, covers, and thumbnails cannot mix generations. Add a regression that builds a cache for book A, replaces the same path with book B, and proves B is re-indexed.

- **Consolidation sources:** [automation/bug-scan-20260928-0821](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/85dbb5c23d56650e8a8e8885fd12f6d008063217/bugs.md); [automation/bug-scan-20260929-0823](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/6a1c2dd84e3e922a799f9c29176cf2706bafc209/bugs.md); [Drive: 2026-09-29 0823 MDT - automation-bug-scan-20260929-0823 - Instructions](https://docs.google.com/spreadsheets/d/1mGhMQ7hk2vMFB7TIHJQ8JXV-Prl3VV31Pt4vDfr17GA/edit?usp=drivesdk); [Drive: 2026-09-28 08-21 MDT - automation-bug-scan-20260928-0821 - integration instructions](https://docs.google.com/spreadsheets/d/1NIrCpsuEiQg9BhUpEAmU9NfC3y6hxSWLpGNa0aktYWU/edit?usp=drivesdk); [Drive: 2026-09-28 08-21 MDT - automation-bug-scan-20260928-0821 - bugs.md diff](https://docs.google.com/spreadsheets/d/1wfojDTXWDof70_0IlYFDZJH0vAGH_xqQUhiqsXH1TnQ/edit?usp=drivesdk); [Drive: 2026-09-29 0823 MDT - automation-bug-scan-20260929-0823 - Diff](https://docs.google.com/spreadsheets/d/1vJqjCYlDdK_38-EdVcug_G3EVGKhGAIet-6GhueGFhg/edit?usp=drivesdk)

### 121. OPF author metadata is corrupted when Expat splits one creator's text across callbacks

- **Status:** Open.

- **Affected code:** `lib/Epub/Epub/parsers/ContentOpfParser.cpp::characterData()`, `startElement()`, and `endElement()` for `dc:creator`.
- **Trigger / reproduction:** Parse a valid OPF containing one `<dc:creator>` whose character data is delivered in more than one Expat callback. A deterministic test can pad the XML so an ordinary creator name crosses a parser input-buffer boundary, then feed the document through the existing streaming parser.
- **Observed / logically demonstrated failure:** While state is `IN_BOOK_AUTHOR`, every character-data callback executes `if (!author.empty()) author.append(", ");` before appending that callback's bytes. Character-data callbacks are stream fragments, not creator-element boundaries. If one creator arrives as `"Jane"` and `" Doe"`, the stored author becomes `"Jane,  Doe"`. Additional callback splits insert additional commas, even though the OPF contains only one creator. The corrupted value is then copied into `BookMetadata` and persisted in `book.bin`.
- **Likely root cause:** The parser uses callback invocation count as a proxy for the number of `dc:creator` elements.
- **Impact:** Valid EPUBs can display and permanently cache malformed author names depending on XML chunking, entity handling, or parser buffering, so the same metadata can vary with streaming boundaries.
- **Repair direction:** Accumulate character data into a per-element `currentCreator` buffer and append it to the aggregate author string exactly once when `</dc:creator>` closes; add the separator only between completed creator elements. Add tests that feed the same OPF at different chunk sizes and verify identical single- and multi-author metadata.

- **Consolidation sources:** [automation/bug-scan-20260928-0821](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/85dbb5c23d56650e8a8e8885fd12f6d008063217/bugs.md); [automation/bug-scan-20260929-0321](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1119b86c20c4a8024ef0f0b1e3ed416ce0b0e8ea/bugs.md); [automation/bug-scan-20260930-0518](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/82533918d81fda11ae9c6b19d95b071a98f19361/bugs.md); [Drive: 2026-09-30 0518 MDT - automation-bug-scan-20260930-0518 - Diff](https://docs.google.com/spreadsheets/d/1geT64642Lsjo_EaRZhNxF9516LV2BRVIp4stNAnOkjg/edit?usp=drivesdk); [Drive: 2026-09-28 08-21 MDT - automation-bug-scan-20260928-0821 - integration instructions](https://docs.google.com/spreadsheets/d/1NIrCpsuEiQg9BhUpEAmU9NfC3y6hxSWLpGNa0aktYWU/edit?usp=drivesdk); [Drive: 2026-09-28 08-21 MDT - automation-bug-scan-20260928-0821 - bugs.md diff](https://docs.google.com/spreadsheets/d/1wfojDTXWDof70_0IlYFDZJH0vAGH_xqQUhiqsXH1TnQ/edit?usp=drivesdk); [Drive: 2026-09-29 0321 MDT - automation-bug-scan-20260929-0321 - Instructions](https://docs.google.com/spreadsheets/d/1olIEa0GA1zbdPFGAPQbYGSKvj7INIdabz3rmzWNM4x0/edit?usp=drivesdk); [Drive: 2026-09-29 0321 MDT - automation-bug-scan-20260929-0321 - Diff](https://docs.google.com/spreadsheets/d/1UP3gJ6ntPpZsnttw6gXKoPoWZ5guN8F2pWhAClKpZxA/edit?usp=drivesdk); [Drive: 2026-09-30 0518 MDT - automation-bug-scan-20260930-0518 - Instructions](https://docs.google.com/spreadsheets/d/1xB4JDjKoHfw-mCY9pUQL1VP2tWxilaT0WRwdCPryrO8/edit?usp=drivesdk)

### 122. EPUB XML parsers compare literal namespace prefixes, rejecting valid books that use equivalent alternate prefixes

- **Status:** Open.

- **Affected code:** `lib/Epub/Epub/parsers/ContainerParser.cpp`, `ContentOpfParser.cpp`, `TocNavParser.cpp`, and `TocNcxParser.cpp`; failure is surfaced by `lib/Epub/Epub.cpp::findContentOpfFile()` / `parseContentOpf()`.
- **Trigger / reproduction:** Create an otherwise valid EPUB whose `META-INF/container.xml` binds the standard container namespace to a prefix, for example `<ocf:container xmlns:ocf="urn:oasis:names:tc:opendocument:xmlns:container"><ocf:rootfiles><ocf:rootfile .../></ocf:rootfiles></ocf:container>`. XML namespace prefixes are aliases, so this is namespace-equivalent to the usual default-namespace form. Open the EPUB.
- **Observed / logically demonstrated failure:** The parsers are created with `XML_ParserCreate(nullptr)`, so Expat reports qualified names including the source prefix. `ContainerParser` accepts only the literal names `container`, `rootfiles`, and `rootfile`; it therefore never sets `fullPath`, and `Epub::findContentOpfFile()` rejects the book. The same QName coupling appears later: `ContentOpfParser` recognizes only no prefix or the hard-coded `opf:` prefix and requires exactly `dc:` for Dublin Core fields, `TocNavParser` requires literal element names and `epub:type`, and `TocNcxParser` requires unprefixed names. A harmless standards-compliant prefix change can thus reject a book or silently lose metadata/TOC data.
- **Likely root cause:** XML elements and attributes are matched by serialized QName text instead of namespace URI plus local name.
- **Impact:** Standards-valid EPUBs can fail to open or lose title/author/language/chapter navigation depending only on the publisher's namespace-prefix choices.
- **Repair direction:** Parse with namespace processing (for example `XML_ParserCreateNS`) and compare namespace URI/local-name pairs, or normalize QNames consistently while validating the expected namespace. Add fixtures using default namespaces and arbitrary non-`opf`/`dc`/`epub` prefixes for container, OPF metadata/manifest/spine, EPUB 3 nav, and NCX.

- **Consolidation sources:** [automation/bug-scan-20260928-0821](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/85dbb5c23d56650e8a8e8885fd12f6d008063217/bugs.md); [automation/bug-scan-20260929-2226](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/13108f6ab843969240b686a9aa624cfb63470260/bugs.md); [Drive: 2026-09-28 08-21 MDT - automation-bug-scan-20260928-0821 - integration instructions](https://docs.google.com/spreadsheets/d/1NIrCpsuEiQg9BhUpEAmU9NfC3y6hxSWLpGNa0aktYWU/edit?usp=drivesdk); [Drive: 2026-09-28 08-21 MDT - automation-bug-scan-20260928-0821 - bugs.md diff](https://docs.google.com/spreadsheets/d/1wfojDTXWDof70_0IlYFDZJH0vAGH_xqQUhiqsXH1TnQ/edit?usp=drivesdk); [Drive: 2026-09-29 2226 MDT - automation-bug-scan-20260929-2226 - 13108f6 - bugs.md diff](https://docs.google.com/spreadsheets/d/1XIr2_53lXbldoungfg66A_4OCh1SPVNtyG4xYoOIZaY/edit?usp=drivesdk); [Drive: 2026-09-29 2226 MDT - automation-bug-scan-20260929-2226 - 13108f6 - integration instructions](https://docs.google.com/spreadsheets/d/1GX7YqdYIr7lUAV1r91jtVHJFe7M79rteWrEQngf2J68/edit?usp=drivesdk)

### 123. Native stream file API rejects valid SD paths at 256 bytes even though the storage API accepts longer paths

- **Status:** Open.

- **Affected code:** `src/native/NativeStreamBridge.cpp::filePath()` and `openFile()`; contrasted with `src/native/NativePlatformBridge.cpp::mapStoragePath()` / the `T5StorageApi` file operations and the public `lib/NativeApps/include/T5StreamApi.h` contract.
- **Trigger / reproduction:** Create a regular file whose complete native path begins with `/sd/` and is 256–511 bytes long (for example several legal nested directory components plus a short filename). The File Browser/storage path machinery can represent such a path, but call `t5_stream_api_v1::open_file(path, T5_STREAM_FILE_READ, ...)` with that same path.
- **Observed / logically demonstrated failure:** `filePath()` rejects every path for which `strnlen(path, 256) >= 256`, so `openFile()` returns `T5_STREAM_INVALID` before attempting to open the file. The stream ABI documents no 255-byte pathname limit, and the regular storage bridge maps the path into a `std::string` without this cap. Files can therefore be visible/readable through one native storage surface but impossible to open through the stream surface.
- **Likely root cause:** The stream bridge uses a fixed 256-byte validation ceiling as an implementation shortcut instead of applying the same segment/path policy as the shared storage abstraction.
- **Impact:** Stream-based consumers can fail on otherwise valid files selected or created through other firmware APIs, producing a cross-API pathname compatibility hole. Paths between 256 and 511 bytes are especially problematic because they remain representable by the existing File Browser buffers but are rejected by the stream bridge.
- **Repair direction:** Replace the fixed `strnlen(..., 256)` gate with the shared storage path validator (or a documented filesystem-derived bound), keep per-segment traversal checks, and add parity tests proving the storage and stream APIs accept/reject the same valid path set, including 255-, 256-, and 511-byte paths.

- **Consolidation sources:** [automation/bug-scan-20260928-0924](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/ac217fcd00c86f7d807fd8864e4f81f4e34f1a06/bugs.md); [Drive: 2026-09-28 09-24 MDT - automation-bug-scan-20260928-0924 - Instructions](https://docs.google.com/spreadsheets/d/1JTPf4QDgUlX7EQD3OCY7xsLobTcUnEE8rJyXxV3LELk/edit?usp=drivesdk); [Drive: 2026-09-28 09-24 MDT - automation-bug-scan-20260928-0924 - Diff](https://docs.google.com/spreadsheets/d/1dHScILciy8BhoVGpZN3WGEA3ykn5XegoGAmXcn1E4nI/edit?usp=drivesdk)

### 124. EPUB package IRIs with percent-encoded path characters are looked up as literal ZIP names

- **Status:** Open.

- **Affected code:** `lib/Epub/Epub/parsers/ContentOpfParser.cpp::startElement()`, `lib/FsHelpers/FsHelpers.cpp::normalisePath()`, and `lib/Epub/Epub.cpp::readItemContentsToBytes()`, `readItemContentsToStream()`, and `getItemSize()`.
- **Trigger / reproduction:** Build a standards-valid EPUB with a container entry such as `OPS/HTML/file name.xhtml` and reference it from the OPF as `href="HTML/file%20name.xhtml"`. Open the book and navigate to that spine item.
- **Observed / logically demonstrated failure:** The OPF parser concatenates the raw `href` and calls `normalisePath()`, which normalizes slash/dot components but performs no IRI/percent decoding. The resulting cached href remains `OPS/HTML/file%20name.xhtml`. The reader then passes that literal string to `ZipFile`, whose central-directory lookup compares entry names byte-for-byte, so it cannot find the actual `OPS/HTML/file name.xhtml` member. W3C EPUB Reading Systems examples explicitly map a container file named `HTML/file name.xhtml` to a URL/path reference containing `HTML/file%20name.xhtml`.
- **Likely root cause:** EPUB references are treated as filesystem strings rather than parsed/resolved IRIs before mapping the URL path component back to an OCF container pathname.
- **Impact:** Standards-compliant books that percent-encode spaces or other required URL characters can fail to load chapters, stylesheets, images, navigation documents, or covers even though the referenced ZIP members exist.
- **Repair direction:** Resolve OPF/nav/content references as URLs/IRIs relative to their document base, percent-decode only the URL path when mapping to the OCF abstract container, preserve reserved-delimiter semantics, and reject invalid escapes instead of silently rewriting them. Add fixtures for `%20`, UTF-8 percent sequences, fragments, and literal percent characters.

- **Consolidation sources:** [automation/bug-scan-20260928-0924](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/ac217fcd00c86f7d807fd8864e4f81f4e34f1a06/bugs.md); [Drive: 2026-09-28 09-24 MDT - automation-bug-scan-20260928-0924 - Instructions](https://docs.google.com/spreadsheets/d/1JTPf4QDgUlX7EQD3OCY7xsLobTcUnEE8rJyXxV3LELk/edit?usp=drivesdk); [Drive: 2026-09-28 09-24 MDT - automation-bug-scan-20260928-0924 - Diff](https://docs.google.com/spreadsheets/d/1dHScILciy8BhoVGpZN3WGEA3ykn5XegoGAmXcn1E4nI/edit?usp=drivesdk)

### 125. ZIP entries with valid names of 256 bytes or longer are silently unreachable

- **Status:** Open.
- **Affected code:** `lib/ZipFile/ZipFile.cpp::loadAllFileStatSlims()`, `loadFileStatSlim()`, and `findFirstBySuffix()`; the fixed `char itemName[256]` buffers used while scanning the central directory.
- **Trigger / reproduction:** Create an otherwise valid ZIP/EPUB containing an entry whose ZIP filename field is 256 bytes or longer, reference that entry from the EPUB manifest, and open/read it through `ZipFile`.
- **Observed / logically demonstrated failure:** ZIP stores the filename length in a 16-bit field, but all lookup scans only read a name when `nameLen < 256`. Longer names are explicitly skipped. They are never inserted into `fileStatSlimCache`, direct lookup can never compare them to the requested filename, and suffix discovery skips them as well. The archive can load successfully while the referenced entry is reported as missing.
- **Likely root cause:** A 256-byte scratch buffer was turned into a hard archive filename limit instead of using bounded comparison/streaming or allocating according to a validated name length.
- **Impact:** Valid ZIP-backed content can fail solely because an internal resource path exceeds 255 bytes. EPUB chapters, navigation files, stylesheets, images, or other resources with long internal paths become inaccessible even though they are present in the archive.
- **Repair direction:** Decouple central-directory parsing from the fixed scratch size. Compare long names in bounded chunks or allocate a validated temporary buffer, and retain complete cache keys under an explicit resource policy. Add fixtures at 255, 256, and larger filename lengths and verify direct lookup plus suffix discovery.

- **Consolidation sources:** [automation/bug-scan-20260928-0924](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/ac217fcd00c86f7d807fd8864e4f81f4e34f1a06/bugs.md); [automation/bug-scan-20260929-0127-findings](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b126c4282b721ea72cd5972a2779992bfaf6441c/bugs.md); [Drive: 2026-09-28 09-24 MDT - automation-bug-scan-20260928-0924 - Instructions](https://docs.google.com/spreadsheets/d/1JTPf4QDgUlX7EQD3OCY7xsLobTcUnEE8rJyXxV3LELk/edit?usp=drivesdk); [Drive: 2026-09-28 09-24 MDT - automation-bug-scan-20260928-0924 - Diff](https://docs.google.com/spreadsheets/d/1dHScILciy8BhoVGpZN3WGEA3ykn5XegoGAmXcn1E4nI/edit?usp=drivesdk); [Drive: 2026-09-29 0127 MDT - automation-bug-scan-20260929-0127-findings - Instructions](https://docs.google.com/spreadsheets/d/1ad4bmYdzefozwBelKNdUD846o6B6SZ6_W2j2E23XvHw/edit?usp=drivesdk); [Drive: 2026-09-29 0127 MDT - automation-bug-scan-20260929-0127-findings - Diff](https://docs.google.com/spreadsheets/d/1Bl2Lj6n40DNLK8lCt8zLIjps7eeAa74hQyQ376hxUh8/edit?usp=drivesdk)

### 126. HalStorage stream copy reports success after a short output write or premature input read failure

- **Status:** Open.

- **Affected code:** `lib/hal/HalStorage.cpp`, `HalStorage::readFileToStream(const char*, Print&, size_t)`.
- **Trigger / reproduction:** Call `readFileToStream()` on an existing file using a `Print` sink whose `write(buffer, n)` accepts fewer than `n` bytes, or inject an SD read failure that makes `f.read()` return zero/negative while unread data remains.
- **Observed / logically demonstrated failure:** For every successful file read the function calls `out.write(...)` and discards the returned byte count. If the sink short-writes, data is silently lost. Likewise, when `f.available()` is true but `f.read()` returns `<= 0`, the loop merely breaks. After either condition the file is closed and the function unconditionally returns `true`. Callers therefore cannot distinguish a complete copy from truncated output.
- **Likely root cause:** The helper treats opening the source as the success criterion and does not propagate either side of the stream-copy I/O contract.
- **Impact:** Any caller using this common HAL helper to materialize or transform an SD file can accept incomplete output as complete, allowing silent corruption to be cached, uploaded, parsed, or published by the next layer.
- **Repair direction:** Require every output write to equal the input chunk length, treat a read failure before verified EOF as failure, and propagate close/sync errors where the underlying API exposes them. Add tests with a short-writing `Print` implementation and an injected source read failure that verify the helper returns `false` and never reports a truncated transfer as successful.

- **Consolidation sources:** [automation/bug-scan-20260928-1020](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/a6c2e0dcd3345f9dd7946e444b7b8263e921d201/bugs.md); [Drive: 2026-09-28 10-20 MDT - automation-bug-scan-20260928-1020 - Instructions](https://docs.google.com/spreadsheets/d/1mLlBAkKzZu2O97mGoH_rs1TACJzlhD7wx1Xs5oY6EIY/edit?usp=drivesdk); [Drive: 2026-09-28 10-20 MDT - automation-bug-scan-20260928-1020 - Diff](https://docs.google.com/spreadsheets/d/1JXPswBeGnJhUCpvWcsZ020laGpgrq7qhh5Woc7M95MY/edit?usp=drivesdk)

### 127. Status Bar bridge reports an unsaved setting mutation as successful when persistence fails

- **Status:** Open.

- **Affected code:** `src/native/NativeStatusBarBridge.cpp`, `itemActivate()`; consumer `Apps/status_bar_settings.c`, Confirm/tap activation paths.
- **Trigger / reproduction:** Open **Customize Status Bar**, force the settings file write to fail, then toggle Chapter Page Count, Book Progress, Progress Bar, Thickness, Title, Battery, or Clock.
- **Observed / logically demonstrated failure:** `itemActivate()` mutates the live `SETTINGS` field first, calls `SETTINGS.saveToFile()` without checking its result, and always returns `true`. The native app ignores that return value and immediately rerenders from the mutated live settings. The screen therefore shows the new value even though the durable settings remain unchanged; a reload or reboot restores the prior value. This is a separate status-bar API path from the direct Settings bridge persistence bug already tracked as #31.
- **Likely root cause:** Status-bar mutation and persistence are not transactional, and both the bridge and its app consumer assume the save succeeded.
- **Impact:** Users receive a false success indication and can lose status-bar customizations after reboot or settings reload. An SD fault can also leave live and durable UI configuration diverged for the remainder of the session.
- **Repair direction:** Preserve the previous field value, require `saveToFile()` success before returning success, restore the field on failure, and make the app surface a save error instead of silently rerendering the optimistic value. Add an injected-write-failure test for each binary/three-state setting family.

- **Consolidation sources:** [automation/bug-scan-20260928-1020](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/a6c2e0dcd3345f9dd7946e444b7b8263e921d201/bugs.md); [Drive: 2026-09-28 10-20 MDT - automation-bug-scan-20260928-1020 - Instructions](https://docs.google.com/spreadsheets/d/1mLlBAkKzZu2O97mGoH_rs1TACJzlhD7wx1Xs5oY6EIY/edit?usp=drivesdk); [Drive: 2026-09-28 10-20 MDT - automation-bug-scan-20260928-1020 - Diff](https://docs.google.com/spreadsheets/d/1JXPswBeGnJhUCpvWcsZ020laGpgrq7qhh5Woc7M95MY/edit?usp=drivesdk)

### 128. Recent Books removes entries from the live UI even when the change was not persisted

- **Status:** Open.
- **Affected code:** `src/RecentBooksStore.cpp`, especially `RecentBooksStore::removeBook()`, `addBook()`, `updateBook()`, and `updatePath()`; `src/activities/home/RecentBooksActivity.cpp::removeSelectedRecentBook()`.
- **Trigger / reproduction:** Populate Recent Books, then make `/.crosspoint/recent.json` unwritable or force its save to fail. Open Recent Books, long-press a row, confirm Delete, and then reload the store or reboot.
- **Observed / logically demonstrated failure:** `removeBook()` erases the matching entry from `recentBooks`, calls `saveToFile()` without checking the return value, and returns `true` unconditionally after the erase. `removeSelectedRecentBook()` then ignores even that return value, removes the row from its local list, and redraws the UI as though the deletion succeeded. The durable JSON can still contain the entry, so it reappears after reload/reboot. `addBook()`, `updateBook()`, and `updatePath()` have the same mutate-then-ignore-save pattern and can likewise leave the live list ahead of durable state.
- **Likely root cause:** Recent-book mutations are published to live state before persistence succeeds, and the API/callers do not propagate or act on persistence failure.
- **Impact:** The UI can positively present a delete or metadata/path update that was never committed. Recent history can unexpectedly reappear or revert after reboot, and later operations run against state that differs from the on-disk source of truth.
- **Repair direction:** Stage the candidate recent-book vector, persist it transactionally, and publish it only after a successful save. Make every mutator return a persistence result; in the activity, keep the row selected and surface an error when removal fails. Add fault-injection tests for add, update, path change, and delete proving both live and durable state remain unchanged on save failure.

- **Consolidation sources:** [automation/bug-scan-20260928-1124](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/13718ced12a20f881425e7b86e84cb725f5cbb24/bugs.md); [automation/bug-scan-20260929-0823](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/6a1c2dd84e3e922a799f9c29176cf2706bafc209/bugs.md); [Drive: 2026-09-28 11-24 MDT - automation_bug-scan-20260928-1124 - Instructions](https://docs.google.com/spreadsheets/d/1bc0LhJFvghzQD4lG5fIguO2E8IzypnacKMLODbx0MMc/edit?usp=drivesdk); [Drive: 2026-09-29 0823 MDT - automation-bug-scan-20260929-0823 - Instructions](https://docs.google.com/spreadsheets/d/1mGhMQ7hk2vMFB7TIHJQ8JXV-Prl3VV31Pt4vDfr17GA/edit?usp=drivesdk); [Drive: 2026-09-28 11-24 MDT - automation_bug-scan-20260928-1124 - Diff](https://docs.google.com/spreadsheets/d/1ZCPXhFC9yJ3WXqiZnDWY98oJoW-e8pEbTKNj9K06yOY/edit?usp=drivesdk); [Drive: 2026-09-28 11-24 MDT - automation_bug-scan-20260928-1124 - Diff](https://docs.google.com/spreadsheets/d/1RhtnthMmMFylAcDRBVDXA0Ysb68RWhSBuZ5nXiEQ6K8/edit?usp=drivesdk); [Drive: 2026-09-29 0823 MDT - automation-bug-scan-20260929-0823 - Diff](https://docs.google.com/spreadsheets/d/1vJqjCYlDdK_38-EdVcug_G3EVGKhGAIet-6GhueGFhg/edit?usp=drivesdk)

### 129. Installed-app path resolution fails globally when /Apps contains more than 512 directory entries

- **Status:** Open.
- **Affected code:** `src/native/InstalledAppPath.cpp::resolveInstalledAppPath()`, especially `kMaxDirectoryEntries = 512`, the bounded `openNextFile()` loop, and the `if (!reachedEnd) return false` guard.
- **Trigger / reproduction:** Install a valid canonical app under `/Apps/<id>/` whose artifact can be resolved by basename, then add enough unrelated valid files/directories under `/Apps` that the directory has at least 513 entries. Resolve the artifact with `resolveInstalledAppPath()`; the target can be placed among the first entries to show that finding it is not sufficient.
- **Observed / logically demonstrated failure:** The resolver records a valid matching managed path while scanning, but `reachedEnd` is set only when `openNextFile()` reports end-of-directory. If 512 entries are consumed without reaching the end, the loop exits at the fixed bound and the function immediately returns `false` before publishing an already-found unique match or trying the legacy fallback. Consequently, merely exceeding the scan bound makes every lookup through this resolver fail, including targets that were verified early in the scan.
- **Likely root cause:** A watchdog/resource guard is also being used as a correctness condition for the entire inventory; unrelated directory entries count against the limit, and a bounded scan has no resumable/indexed continuation.
- **Impact:** A sufficiently large or cluttered `/Apps` directory can make otherwise valid installed applications unresolvable through basename-based launch/recovery paths. The failure is inventory-wide rather than limited to entries after the 512th position.
- **Repair direction:** Resolve through the authoritative installed-package index, or page/resume directory enumeration until ambiguity can be decided without imposing a correctness-changing entry cap. If a hard resource ceiling is unavoidable, return an explicit resource/inventory error rather than treating a verified target as not found. Add tests at 512 and 513 entries with the target both before and after the boundary, plus an ambiguity case.

- **Consolidation sources:** [automation/bug-scan-20260928-1124](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/13718ced12a20f881425e7b86e84cb725f5cbb24/bugs.md); [Drive: 2026-09-28 11-24 MDT - automation_bug-scan-20260928-1124 - Instructions](https://docs.google.com/spreadsheets/d/1bc0LhJFvghzQD4lG5fIguO2E8IzypnacKMLODbx0MMc/edit?usp=drivesdk); [Drive: 2026-09-28 11-24 MDT - automation_bug-scan-20260928-1124 - Diff](https://docs.google.com/spreadsheets/d/1ZCPXhFC9yJ3WXqiZnDWY98oJoW-e8pEbTKNj9K06yOY/edit?usp=drivesdk); [Drive: 2026-09-28 11-24 MDT - automation_bug-scan-20260928-1124 - Diff](https://docs.google.com/spreadsheets/d/1RhtnthMmMFylAcDRBVDXA0Ysb68RWhSBuZ5nXiEQ6K8/edit?usp=drivesdk)

### 130. Markdown fenced-code state can be closed by the wrong fence marker and corrupt parsing of the rest of the document

- **Status:** Open.
- **Affected code:** `lib/Markdown/Markdown.cpp`, `isFence()` and `parseLine(std::string_view, bool&)`; corresponding public state shape in `lib/Markdown/Markdown.h`.
- **Trigger / reproduction:** Parse Markdown containing a backtick fence with a tilde-fence-looking line inside it, for example an opening three-backtick fence, a code line, a line containing `~~~`, another code line, and then the closing three-backtick fence. The inverse case with a tilde opener and backticks inside behaves the same way.
- **Observed / logically demonstrated failure:** `isFence()` returns true for any trimmed line whose first three characters are either backticks or tildes. `parseLine()` stores only a boolean `inFence` and toggles it on every such line, so it cannot remember which marker opened the block or how long the opening run was. A `~~~` line therefore closes a backtick block; the following code is parsed as normal Markdown, and the actual backtick closer toggles the parser back into code mode. Fence-like lines with trailing text can likewise toggle state even when they should remain code content rather than act as a closing delimiter.
- **Likely root cause:** Fenced-code parsing models the state as an on/off flag instead of retaining the opening delimiter character and run length and validating a compatible closer.
- **Impact:** Valid Markdown can render code as prose, suppress or invent later headings/chapter breaks, and leave the remainder of the document in the wrong parse mode.
- **Repair direction:** Replace the boolean-only fence state with a small delimiter state containing marker type and opening run length. Open on a valid fence, and close only on the compatible marker with a sufficient run and valid closing-line suffix; otherwise emit the line as code content. Add regression cases for mixed backtick/tilde markers, longer opening runs, fence-like text inside code, trailing text, and end-of-file while a fence remains open.

- **Consolidation sources:** [automation/bug-scan-20260928-1124](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/13718ced12a20f881425e7b86e84cb725f5cbb24/bugs.md); [automation/bug-scan-20260929-2226](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/13108f6ab843969240b686a9aa624cfb63470260/bugs.md); [Drive: 2026-09-28 11-24 MDT - automation_bug-scan-20260928-1124 - Instructions](https://docs.google.com/spreadsheets/d/1bc0LhJFvghzQD4lG5fIguO2E8IzypnacKMLODbx0MMc/edit?usp=drivesdk); [Drive: 2026-09-28 11-24 MDT - automation_bug-scan-20260928-1124 - Diff](https://docs.google.com/spreadsheets/d/1ZCPXhFC9yJ3WXqiZnDWY98oJoW-e8pEbTKNj9K06yOY/edit?usp=drivesdk); [Drive: 2026-09-28 11-24 MDT - automation_bug-scan-20260928-1124 - Diff](https://docs.google.com/spreadsheets/d/1RhtnthMmMFylAcDRBVDXA0Ysb68RWhSBuZ5nXiEQ6K8/edit?usp=drivesdk); [Drive: 2026-09-29 2226 MDT - automation-bug-scan-20260929-2226 - 13108f6 - bugs.md diff](https://docs.google.com/spreadsheets/d/1XIr2_53lXbldoungfg66A_4OCh1SPVNtyG4xYoOIZaY/edit?usp=drivesdk); [Drive: 2026-09-29 2226 MDT - automation-bug-scan-20260929-2226 - 13108f6 - integration instructions](https://docs.google.com/spreadsheets/d/1GX7YqdYIr7lUAV1r91jtVHJFe7M79rteWrEQngf2J68/edit?usp=drivesdk)

### 131. Hollow Trail disables all built-in controls whenever any XInput controller is connected

- **Status:** Open.
- **Affected code:** `Apps/hollow_trail.c`, `ht_input()` and the native-app Back ownership set in `app_main()`.
- **Trigger / reproduction:** Launch Hollow Trail with an XInput receiver/controller connected so `snapshot()` returns at least one state with `connected != 0`. Leave the controller idle and press the device's built-in Left/Right/Confirm/Up/Down/Back controls.
- **Observed / logically demonstrated failure:** `ht_input()` sets a single `connected` flag as soon as any gamepad is present and executes the built-in-button mapping only under `if (!connected)`. Therefore every built-in gameplay/navigation button is discarded while an XInput device is merely connected. Hollow Trail also calls `set_back_exits_app(false)`, so the physical Back button is not rescued by the host's normal native-app exit policy; the app can require the connected controller (or another global exit path) even though the device buttons are functional.
- **Likely root cause:** Direct gamepad input was implemented as a mutually exclusive fallback selector instead of an additional input source. Presence of a gamepad is being confused with ownership of every physical control.
- **Impact:** Plugging in or leaving a receiver attached disables on-device movement, jump, inspect, pause, and Back/exit controls. An idle, inaccessible, or partially working controller can make the game effectively controller-only and removes the device controls as a recovery path.
- **Repair direction:** Merge built-in button state with direct XInput state rather than skipping it when a controller is connected. If navigation-bridge aliases need de-duplication, suppress only duplicated controller-derived aliases with source-aware logic, not physical buttons. Add a regression test with an idle connected XInput snapshot and verify each built-in control, especially Back, still works.

- **Consolidation sources:** [automation/bug-scan-20260928-1222](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/ea0d1ba79429d443834aa7f780669451b3ee7a75/bugs.md); [Drive: 2026-09-28 1222 MDT - automation-bug-scan-20260928-1222 - Instructions](https://docs.google.com/spreadsheets/d/11dzIWrzE51sVytvkiPaYovT0kl7HSp4XiIjfX-PNStE/edit?usp=drivesdk); [Drive: 2026-09-28 1222 MDT - automation-bug-scan-20260928-1222 - Diff](https://docs.google.com/spreadsheets/d/1zgjOdDRQ5oaXqBc4goLw4znfVL7TJMq4obDZFF5dLVo/edit?usp=drivesdk)

### 132. Native HTTP session cookies work only on the insecure-HTTPS transport path

- **Status:** Open.
- **Affected code:** `src/native/NativeNetworkBridge.cpp`, `httpRequest()`, `performInsecureHttps()`, `nativeNetworkBegin()`, and `nativeNetworkEnd()`; session-cookie flags in `lib/NativeApps/include/T5NetworkApi.h`.
- **Trigger / reproduction:** During one native-app session, make an HTTPS request with a non-empty `cert_pem` to an endpoint that returns `Set-Cookie`, then make a second request that requires that cookie. The same defect affects plain HTTP requests handled by the ESP HTTP client path.
- **Observed / logically demonstrated failure:** The per-app `nativeCookieJar` is attached only inside `performInsecureHttps()` via `HTTPClient::setCookieJar()`. `httpRequest()` takes that path only for HTTPS when the caller supplies no certificate. Supplying a CA certificate sends the request through `esp_http_client`, where the bridge neither stores response cookies nor emits stored cookies on later requests, and it never sets the cookie availability/storage flags. Thus the native-app cookie session silently disappears when the caller uses certificate-verified HTTPS (or HTTP).
- **Likely root cause:** The network bridge has two HTTP implementations, but cookie-session handling was added only to the Arduino `HTTPClient` compatibility path.
- **Impact:** Multi-request authenticated services can succeed when TLS verification is disabled yet lose their login/session state when the app correctly supplies a CA certificate. This creates transport-dependent behavior in the same `http_request` API and can break logins, redirects, or authenticated follow-up requests.
- **Repair direction:** Give both transport paths the same bounded cookie-session semantics: parse/store `Set-Cookie`, emit matching `Cookie` headers, apply the existing size/count bounds, and set the result flags consistently. Prefer one shared cookie layer above both clients. Add two-request regression tests for certificate-verified HTTPS, insecure HTTPS, and HTTP, including redirect/session cases.

- **Consolidation sources:** [automation/bug-scan-20260928-1222](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/ea0d1ba79429d443834aa7f780669451b3ee7a75/bugs.md); [Drive: 2026-09-28 1222 MDT - automation-bug-scan-20260928-1222 - Instructions](https://docs.google.com/spreadsheets/d/11dzIWrzE51sVytvkiPaYovT0kl7HSp4XiIjfX-PNStE/edit?usp=drivesdk); [Drive: 2026-09-28 1222 MDT - automation-bug-scan-20260928-1222 - Diff](https://docs.google.com/spreadsheets/d/1zgjOdDRQ5oaXqBc4goLw4znfVL7TJMq4obDZFF5dLVo/edit?usp=drivesdk)

### 133. A 129-entry application catalog makes the catalog service fail instead of remaining usable

- **Status:** Open.
- **Affected code:** `src/native/AppCatalogIndex.cpp`, `kMaxCatalogEntries` and `fetchAppCatalogIndex()`; `src/native/NativeAppHost.cpp`, `kMaxCatalogAssets`, `loadIndependentAppIndex()`, `CatalogReleaseStream`, and `loadAvailableAppCatalog()`.
- **Trigger / reproduction:** Publish an otherwise valid schema-1 release index or aggregate app catalog containing 129 application records, then refresh App Store.
- **Observed / logically demonstrated failure:** Both catalog parsers hard-limit the service to 128 entries. `loadIndependentAppIndex()` returns false as soon as the authoritative index has more than `kMaxCatalogAssets`; `fetchAppCatalogIndex()` likewise rejects an aggregate catalog whose `apps` array exceeds `kMaxCatalogEntries`. When a release advertises an aggregate catalog and that load fails, `loadAvailableAppCatalog()` explicitly clears the catalog and refuses the per-app fallback. The result is an unavailable/empty catalog, not merely omission of entries after 128. This is distinct from the existing App Store 64-row UI bug: that bug hides later rows from a valid provider catalog, while this defect causes the provider catalog itself to fail once it grows past 128.
- **Likely root cause:** A fixed early catalog-size bound was retained after catalog JSON and vectors moved to PSRAM-backed/bounded metadata handling, and overflow is treated as structural invalidity rather than a capacity condition.
- **Impact:** Adding the 129th valid app can turn catalog growth into a full App Store outage, including the first 128 otherwise-valid apps. External providers synchronized into the authoritative index count toward the same failure threshold.
- **Repair direction:** Replace the entry-count rejection with a byte/memory budget and PSRAM-backed dynamic indexing, or add explicit paging/chunking to the release index. If a hard device limit must remain, do not invalidate the entire catalog: expose a bounded usable prefix plus an explicit truncation/error state. Add tests for 128, 129, and larger authoritative/aggregate catalogs and verify the first entries remain available.

- **Consolidation sources:** [automation/bug-scan-20260928-1222](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/ea0d1ba79429d443834aa7f780669451b3ee7a75/bugs.md); [Drive: 2026-09-28 1222 MDT - automation-bug-scan-20260928-1222 - Instructions](https://docs.google.com/spreadsheets/d/11dzIWrzE51sVytvkiPaYovT0kl7HSp4XiIjfX-PNStE/edit?usp=drivesdk); [Drive: 2026-09-28 1222 MDT - automation-bug-scan-20260928-1222 - Diff](https://docs.google.com/spreadsheets/d/1zgjOdDRQ5oaXqBc4goLw4znfVL7TJMq4obDZFF5dLVo/edit?usp=drivesdk)

### 134. Moving a finished EPUB to /Read leaves its bookmarks behind under the old path-derived filename

- **Status:** Open.

- **Affected code:** `src/activities/reader/EpubReaderActivity.cpp`, `EpubReaderActivity::moveFinishedBookToReadFolder()`; `src/util/BookmarkUtil.cpp`, `BookmarkUtil::getBookmarkPath()`; bookmark loading in `EpubReaderActivity::loadCachedBookmarks()` and `EpubReaderBookmarksActivity::onEnter()`.
- **Trigger / reproduction:** Enable **Move finished books to /Read**, add one or more bookmarks to an EPUB outside `/Read`, finish the book so `moveFinishedBookToReadFolder()` runs, then reopen the moved EPUB from `/Read` and open its bookmark list.
- **Observed / logically demonstrated failure:** The move routine renames the EPUB, attempts to rename its EPUB cache directory, updates Recent Books, and updates `APP_STATE.openEpubPath`, but it never moves the bookmark JSON. Bookmark storage is derived from the full book pathname: for example, `/Books/Example.epub` maps to `/.crosspoint/bookmarks/Books_Example.json`, while the moved `/Read/Example.epub` maps to `/.crosspoint/bookmarks/Read_Example.json`. On the next open, the reader looks only at the new pathname-derived bookmark file, so the existing bookmarks disappear from the UI while the old JSON is orphaned.
- **Likely root cause:** The finished-book relocation transaction updates the EPUB/cache/recents/open-path state but omits another path-keyed sidecar store.
- **Impact:** Finishing a book can make all of that book's saved bookmarks inaccessible exactly when the automatic move feature succeeds. Adding new bookmarks after the move creates a second bookmark store, making later recovery/merging ambiguous.
- **Repair direction:** Before or as part of the EPUB move transaction, derive both old and new bookmark paths and migrate the existing bookmark file with collision/error handling. Treat sidecar migration as part of a coherent relocation transaction, and add a regression that bookmarks a book, finishes/moves it, reopens it from `/Read`, and verifies the same bookmarks remain available.

- **Consolidation sources:** [automation/bug-scan-20260928-1321](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b5bb23216aff4f9d1565fe127c00e99f3c8e268b/bugs.md); [Drive: 2026-09-28 1321 MDT - automation-bug-scan-20260928-1321 - Instructions](https://docs.google.com/spreadsheets/d/1SD5vp5rxtrL_Mp80xBG_6bKaAVb5R0WaeVM6gS3-Tzs/edit?usp=drivesdk); [Drive: 2026-09-28 1321 MDT - automation-bug-scan-20260928-1321 - Diff](https://docs.google.com/spreadsheets/d/1C7xQuBintsL2WCAzJohJICs00HGoUDs5ir8iAm0eTCg/edit?usp=drivesdk)

### 135. 3D Model Viewer leaks the raw-touch provider lease when touch setup fails after acquisition

- **Status:** Open.

- **Affected code:** `Apps/model_viewer.c`, especially `mv_touch_begin()`, `mv_touch_end()`, and the touch-setup failure path in `app_main()`.
- **Trigger / reproduction:** Launch Model Viewer with an installed `input.touch.raw` provider whose capability acquisition succeeds and returns a lease/interface, but whose exposed ABI is invalid/incomplete or whose `subscribe()` call returns 0. Then leave the resulting **TOUCH UNAVAILABLE** screen.
- **Observed / logically demonstrated failure:** `mv_touch_begin()` stores the acquired lease in `g_touch_lease` and the interface in `g_touch` before validating the ABI and before subscribing. On either later failure it simply returns `false`. The caller displays the error, stops video, frees the model, and returns without calling `mv_touch_end()`. The acquired provider lease therefore remains unreleased even though the app never became usable.
- **Likely root cause:** Touch acquisition is multi-stage, but cleanup is only wired into the normal app shutdown path instead of every post-acquire failure path.
- **Impact:** A bad or transiently failing touch provider can leave a capability lease alive after Model Viewer exits, retaining provider resources and potentially making later provider activation/update/uninstall fail or report busy until a broader runtime cleanup/reboot.
- **Repair direction:** Make `mv_touch_begin()` transactional: on any failure after acquisition, unsubscribe if needed, release the lease, and clear all touch globals before returning `false`. Alternatively, have the caller always invoke a safe/idempotent `mv_touch_end()` on failed setup. Add fixtures for invalid ABI and subscribe failure and assert the acquired lease is released exactly once.

- **Consolidation sources:** [automation/bug-scan-20260928-1420](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/ef39eb348e91adede6760ac5ba69e3c4b3e322ee/bugs.md); [Drive: 2026-09-28 14-20 MDT - automation_bug-scan-20260928-1420 - Instructions](https://docs.google.com/spreadsheets/d/1t3MMT4kWW4s0JxkgB3xHJx8Eh4EmjODAYb8jnaEvG0Q/edit?usp=drivesdk); [Drive: 2026-09-28 14-20 MDT - automation_bug-scan-20260928-1420 - Diff](https://docs.google.com/spreadsheets/d/1hQX8mL5jHM8im48M3RT_JSD2hN71FrC9UHXEa5qx8j0/edit?usp=drivesdk)

### 136. 3D Model Viewer silently truncates long OBJ records and renders incomplete valid faces

- **Status:** Open.

- **Affected code:** `Apps/model_viewer.c`, `MV_LINE_CAP`, `mv_reader_line()`, `mv_obj_face_tokens()`, and both passes of `mv_load_obj()`.
- **Trigger / reproduction:** Open a syntactically valid OBJ containing a face record longer than 1023 bytes, for example one polygon with enough vertex/texture/normal index tokens that its single `f ...` line exceeds `MV_LINE_CAP - 1`.
- **Observed / logically demonstrated failure:** `mv_reader_line()` stops copying once the fixed 1024-byte destination is full but continues consuming bytes until the newline and returns success without any truncation flag. Both OBJ passes therefore parse only the retained prefix. The first pass allocates for the truncated token count and the second triangulates the same prefix, so loading succeeds and the omitted portion of the valid polygon is silently discarded rather than causing a load error.
- **Likely root cause:** The line reader conflates “complete line” and “prefix of an over-capacity line”; the OBJ loader has no way to distinguish them.
- **Impact:** Large but valid OBJ polygons can be displayed with missing triangles/surfaces while the UI reports a successful model load, making geometry inspection unreliable.
- **Repair direction:** Have `mv_reader_line()` report overflow separately and make OBJ/STL text parsing reject an overlong structural record, or use a growable/PSRAM-backed record buffer with an explicit practical bound. Add a regression with a >1023-byte face line and verify either the complete polygon is triangulated or the file is rejected explicitly, never partially accepted.

- **Consolidation sources:** [automation/bug-scan-20260928-1420](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/ef39eb348e91adede6760ac5ba69e3c4b3e322ee/bugs.md); [automation/bug-scan-20260929-2102](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/728e209d547481d1060b7841b4e22c5eb837ff8c/bugs.md); [Drive: 2026-09-28 14-20 MDT - automation_bug-scan-20260928-1420 - Instructions](https://docs.google.com/spreadsheets/d/1t3MMT4kWW4s0JxkgB3xHJx8Eh4EmjODAYb8jnaEvG0Q/edit?usp=drivesdk); [Drive: 2026-09-28 14-20 MDT - automation_bug-scan-20260928-1420 - Diff](https://docs.google.com/spreadsheets/d/1hQX8mL5jHM8im48M3RT_JSD2hN71FrC9UHXEa5qx8j0/edit?usp=drivesdk); [Drive: 2026-09-29 21-02 MDT - automation-bug-scan-20260929-2102 - Instructions](https://docs.google.com/spreadsheets/d/10o78oxZAN0DYrcpzIBXxrHkgQLsr9hocvSpV1IfPSEY/edit?usp=drivesdk); [Drive: 2026-09-29 21-02 MDT - automation-bug-scan-20260929-2102 - Diff](https://docs.google.com/spreadsheets/d/1llLFetNI6nEmi67v1zvyKkLU9xAapNH61elrKCuOF8s/edit?usp=drivesdk)

### 137. WebDAV COPY can report success after a premature source read and publish a truncated destination

- **Status:** Open.

- **Affected code:** `src/network/WebDAVHandler.cpp`, `WebDAVHandler::handleCopy()`, specifically the streaming copy loop and `copyOk` success decision.
- **Trigger / reproduction:** Issue WebDAV `COPY` for a regular file and inject an SD/source read failure after at least one chunk has copied but before the source's declared size is exhausted.
- **Observed / logically demonstrated failure:** The loop runs while `srcFile.available()`, calls `srcFile.read()`, and executes `break` when `bytesRead <= 0` without setting `copyOk = false` or checking the number of bytes copied against the original source size. After closing both files, `copyOk` is still true, so the server returns 201/204 and retains the shorter destination. Only short destination writes are treated as errors.
- **Likely root cause:** Premature/failed source reads are treated as ordinary EOF, and successful completion is inferred from absence of an output-write error rather than verified byte-count completion.
- **Impact:** A transient SD read fault can silently create or overwrite a WebDAV destination with truncated data while the client is told the COPY succeeded.
- **Repair direction:** Snapshot the expected source size before copying, track bytes read/written, fail on any zero/negative read before the expected count is reached, and require the final copied count to equal the source size before publishing success. Prefer staging the destination so failure cannot expose a partial file. Add a fault-injection test that fails a mid-copy source read and verifies an error response and no truncated published destination.

- **Consolidation sources:** [automation/bug-scan-20260928-1420](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/ef39eb348e91adede6760ac5ba69e3c4b3e322ee/bugs.md); [Drive: 2026-09-28 14-20 MDT - automation_bug-scan-20260928-1420 - Instructions](https://docs.google.com/spreadsheets/d/1t3MMT4kWW4s0JxkgB3xHJx8Eh4EmjODAYb8jnaEvG0Q/edit?usp=drivesdk); [Drive: 2026-09-28 14-20 MDT - automation_bug-scan-20260928-1420 - Diff](https://docs.google.com/spreadsheets/d/1hQX8mL5jHM8im48M3RT_JSD2hN71FrC9UHXEa5qx8j0/edit?usp=drivesdk)

### 138. Manage Fonts silently hides catalog families after row 64

- **Status:** Open.

- **Affected code:** `Apps/font_manager.c`, `MAX_FAMILIES`, `load_rows()`, `activate_selected()`; `src/native/NativeFontBridge.cpp::familyCount()`.
- **Trigger / reproduction:** Provide a valid font manifest containing more than 64 families, refresh **Manage Fonts**, then try to install, update, or remove a family whose catalog index is 64 or greater.
- **Observed / logically demonstrated failure:** The native font service reports the full `families.size()`, with no 64-family catalog limit, but `load_rows()` clamps that count to `MAX_FAMILIES == 64`. No paging, continuation, search, or truncation warning exists. Families after the first 64 never receive a row and `activate_selected()` can never address them.
- **Likely root cause:** The fixed render-array capacity is also being used as the total catalog-enumeration limit instead of as a bounded visible/page window.
- **Impact:** Valid catalog entries can become impossible to install or update through Manage Fonts. This is distinct from bug #60, which truncates the separate **Font Family** selector for already-installed SD families; this defect truncates the install/update/delete catalog workflow itself.
- **Repair direction:** Page or virtualize the full `family_count()` result while retaining the real catalog index for each visible row, or dynamically allocate the bounded row model in PSRAM. Show explicit truncation only if a hard service limit is unavoidable. Add tests with 65+ manifest families and actions on an entry beyond row 64.

- **Consolidation sources:** [automation/bug-scan-20260928-1520](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/2d13c5df568d1a5cb37e4c0aa785e7c6feb861c4/bugs.md); [Drive: 2026-09-28 15-20 MDT - automation-bug-scan-20260928-1520 - Instructions](https://docs.google.com/spreadsheets/d/1wYRFUPOyFqzGet9bQ8tRNpDaAQDcRRdr8DI3vts2vMk/edit?usp=drivesdk); [Drive: 2026-09-28 15-20 MDT - automation-bug-scan-20260928-1520 - Diff](https://docs.google.com/spreadsheets/d/17mygQzs_2CezszJBGTo2MCfbpOCUlNDblXwdtoHkOZE/edit?usp=drivesdk)

### 139. Setting a BMP as the sleep cover can publish a truncated image after a source read failure

- **Status:** Open.

- **Affected code:** `src/activities/util/BmpViewerActivity.cpp`, `BmpViewerActivity::doSetSleepCover()`.
- **Trigger / reproduction:** Open a valid BMP and choose **Set Sleep Cover**, then induce an SD read failure after some source bytes have been copied but before EOF.
- **Observed / logically demonstrated failure:** The copy loop initializes `success = true` and changes it only when the destination `write()` is short. A source `read()` returning 0 or a negative value terminates the loop exactly like normal EOF, without comparing bytes copied to the source file size or checking a read-error state. The code then removes the previous `/sleep.bmp`, renames the incomplete `/sleep.bmp.tmp` into place, marks the custom sleep-screen setting active, and reports success.
- **Likely root cause:** End-of-input and source I/O failure are conflated, and the staged copy is committed without proving that the expected source byte count was transferred.
- **Impact:** A transient SD fault can replace a known-good sleep cover with a truncated/corrupt BMP while the UI reports success; subsequent sleep rendering can fail or show a damaged image.
- **Repair direction:** Capture the source size before copying, accumulate transferred bytes, treat any premature zero/negative read as failure, and commit the temporary file only after exactly the expected byte count is copied and flushed. Preserve the old `/sleep.bmp` until the new staged file is fully verified, and add a fault-injected short-read regression test.

- **Consolidation sources:** [automation/bug-scan-20260928-1520](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/2d13c5df568d1a5cb37e4c0aa785e7c6feb861c4/bugs.md); [Drive: 2026-09-28 15-20 MDT - automation-bug-scan-20260928-1520 - Instructions](https://docs.google.com/spreadsheets/d/1wYRFUPOyFqzGet9bQ8tRNpDaAQDcRRdr8DI3vts2vMk/edit?usp=drivesdk); [Drive: 2026-09-28 15-20 MDT - automation-bug-scan-20260928-1520 - Diff](https://docs.google.com/spreadsheets/d/17mygQzs_2CezszJBGTo2MCfbpOCUlNDblXwdtoHkOZE/edit?usp=drivesdk)

### 140. GPS continues displaying stale coordinates after a runtime read failure

- **Status:** Open.

- **Affected code:** `Apps/gps.c::app_main()` and `render()`; failure contract in `src/runtime/drivers/GpsDriverRuntime.cpp::read()` and `src/native/NativeGpsBridge.cpp::readState()`.
- **Trigger / reproduction:** Start GPS successfully, obtain a valid fix so `state` contains coordinates, then force a later provider/runtime read to fail (for example loss of the active driver/lease or a provider read failure) while leaving the GPS app open.
- **Observed / logically demonstrated failure:** Both the initial read after `gps->start()` and every loop read call `gps->read(&state)` without checking its boolean result. `GpsDriverRuntime::read()` explicitly returns `false` on lost authorization/stream state or provider failure and stops the runtime; the provider-failure path does not guarantee that the caller's `state` has been replaced with a clean OFF snapshot. The app then continues to render the same `state` every three seconds. After a previously valid fix, the screen can therefore keep showing the old latitude/longitude, satellite count, and “Fix age” footer after the runtime has failed and stopped.
- **Likely root cause:** The app treats `read()` as a void refresh operation even though the API uses its return value to distinguish a valid state snapshot from an I/O/runtime failure.
- **Impact:** The GPS screen can present obsolete position data as the current coordinates precisely when the underlying GNSS provider has failed, which is materially misleading for a location utility.
- **Repair direction:** Check every `gps->read()` result. On failure, clear the snapshot, set an explicit OFF/error state, render the failure immediately, and optionally offer/retry a controlled restart instead of continuing to display old data. Add a test that supplies one valid fix followed by a failed read and verifies the old coordinates disappear.

- **Consolidation sources:** [automation/bug-scan-20260928-1623](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/0b0cd92cfb5cafc7d4b5baa897442b2acb9b1030/bugs.md); [Drive: 2026-09-28 16-23 MDT - automation_bug-scan-20260928-1623 - Instructions](https://docs.google.com/spreadsheets/d/1j8Lm6ObGsHFSC50yu0IP8iFt5c14EkbJsHcCGh9edpI/edit?usp=drivesdk); [Drive: 2026-09-28 16-23 MDT - automation_bug-scan-20260928-1623 - Diff](https://docs.google.com/spreadsheets/d/1j_TEfpX-sHjsgfaRCxGHsuRPpo8pn12zPHTNYv1jGkw/edit?usp=drivesdk)

### 141. Native installed-app discovery stops after 128 valid apps and makes later apps unreachable from Springboard

- **Status:** Open.

- **Affected code:** `src/native/NativeAppHost.cpp::installedRefresh()`, `installedCount()`, and `installedGet()`; consumer `Apps/springboard.c::app_main()`. This is distinct from bug #15 (64-row available App Store catalog) and bug #62 (64-row Package Manager view): this limit truncates the firmware's installed **application** inventory itself.
- **Trigger / reproduction:** Put at least 129 valid launchable applications under `/Apps` (canonical managed app directories and/or valid legacy ELF/JSON pairs), with the 129th valid app occurring after 128 other valid apps in directory enumeration, then refresh/open Springboard.
- **Observed / logically demonstrated failure:** `installedRefresh()` enumerates only while `s->installed.size() < 128`. As soon as the 128th valid manifest is accepted it stops reading the directory entirely, sorts only that prefix, and returns success. `installedCount()` can therefore report at most 128 even though additional valid apps exist; Springboard also clamps its count to 128 and has no continuation or overflow indication. Which applications disappear depends on SD directory enumeration order, not display-name sort order.
- **Likely root cause:** A memory-protection bound in the installed-app cache became an implicit inventory limit, and the API exposes neither paging nor overflow state.
- **Impact:** Valid installed applications beyond the first 128 cannot be discovered, selected, pinned, or launched from Springboard despite being correctly installed on disk; unrelated install/remove operations can change which apps fall outside the prefix.
- **Repair direction:** Enumerate the complete installed-app set into a dynamic/PSRAM-backed structure or expose paged installed-app enumeration with a stable identity/cursor. Remove Springboard's duplicate hard clamp or make it a page capacity rather than a total limit, and add a 129+ app test proving the final app remains addressable.

- **Consolidation sources:** [automation/bug-scan-20260928-1623](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/0b0cd92cfb5cafc7d4b5baa897442b2acb9b1030/bugs.md); [Drive: 2026-09-28 16-23 MDT - automation_bug-scan-20260928-1623 - Instructions](https://docs.google.com/spreadsheets/d/1j8Lm6ObGsHFSC50yu0IP8iFt5c14EkbJsHcCGh9edpI/edit?usp=drivesdk); [Drive: 2026-09-28 16-23 MDT - automation_bug-scan-20260928-1623 - Diff](https://docs.google.com/spreadsheets/d/1j_TEfpX-sHjsgfaRCxGHsuRPpo8pn12zPHTNYv1jGkw/edit?usp=drivesdk)

### 142. OPDS settings API emits invalid JSON when the first serialized server is oversized

- **Status:** Open.

- **Affected code:** `src/network/CrossPointWebServer.cpp`, `CrossPointWebServer::handleGetOpdsServers()` and `handlePostOpdsServer()`; `src/OpdsServerStore.cpp`, `addServer()` / `updateServer()`.
- **Trigger / reproduction:** Store at least two OPDS servers with the first server's name/URL/username long enough that its JSON object serializes to 512 bytes or more, and keep the second server small enough to fit. This can be created through the POST API because those string fields are accepted into `std::string` without a corresponding 512-byte response-object limit. Then request `GET /api/opds`.
- **Observed / logically demonstrated failure:** `handleGetOpdsServers()` serializes each server into a fixed 512-byte buffer and `continue`s when an object is too large. Comma emission, however, is based on the original loop index (`if (i > 0)`) rather than whether any prior object was actually emitted. If index 0 is skipped and index 1 fits, the response begins `[,` followed by the second object, which is invalid JSON. The web settings client can no longer parse the server list even though a valid later entry exists.
- **Likely root cause:** The streaming JSON writer conflates source position with emitted-element position, and input storage allows records that exceed the response scratch buffer.
- **Impact:** One long first OPDS configuration can break the entire OPDS-management API response for all servers, preventing normal browser-side listing/editing until the data is repaired by another path.
- **Repair direction:** Track an explicit `emittedAny` flag (or stream each object without a fixed serialization cap) and insert commas only between successfully emitted objects. Also impose coherent validated field-size limits at write time or dynamically size the response serialization. Add a regression where element 0 is intentionally skipped/oversized and element 1 is valid, asserting parseable JSON with no leading comma.

- **Consolidation sources:** [automation/bug-scan-20260928-1726](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1e13b691f3ce2fc0e6c3a2b27decde7a2cbb8e7b/bugs.md); [Drive: 2026-09-28 1726 MDT - automation-bug-scan-20260928-1726 - bugs.md diff](https://docs.google.com/spreadsheets/d/1kgrLWNI3FpH5CbvPkMidNJ1Rb8612YIc4wvcHUZTXAc/edit?usp=drivesdk); [Drive: 2026-09-28 1726 MDT - automation-bug-scan-20260928-1726 - integration instructions](https://docs.google.com/spreadsheets/d/1L-AdwyFrRDz-RP_vMgrXG6VZH6JZ3DgdTcFwOdiQGSo/edit?usp=drivesdk); [Drive: 2026-09-28 1726 MDT - automation-bug-scan-20260928-1726 - integration instructions](https://docs.google.com/spreadsheets/d/102h_psLvf3PZUscY-JDkaum55n835woMdUanF829eTg/edit?usp=drivesdk)

### 143. Native archive extraction can publish a destination even when the output file fails to close

- **Status:** Open.

- **Affected code:** `src/native/NativeArchiveBridge.cpp::extract()` and `BoundedOutput`; `HalFile::close()` in `lib/hal/HalStorage.h`.
- **Trigger / reproduction:** Extract a valid ZIP entry while injecting an SD/filesystem failure that occurs after all expected bytes have been accepted by `HalFile::write()` but causes the final `HalFile::close()` to return `false`.
- **Observed / logically demonstrated failure:** The extraction path requires `ZipFile::readFileToStream()` and `sink.good()` to succeed, calls `output.flush()`, then calls `output.close()` but discards the boolean close result. If the earlier byte-count checks passed, it proceeds to rename the `.part` file to the final destination. The bridge can therefore return success and publish an extraction whose final filesystem close/commit failed.
- **Likely root cause:** Write-count validation is treated as sufficient completion even though the storage abstraction explicitly exposes close-time failure.
- **Impact:** ROMs or other package resources extracted through the native archive API can be reported as successfully installed while the destination is incomplete or not durably committed. The staged `.part` design does not protect the final path if publication occurs after a failed close.
- **Repair direction:** Require a successful `close()` before renaming the staged file; if close fails, remove or quarantine the `.part` file and return failure. Add fault-injection coverage where writes reach the expected size but close fails, and verify that no final destination is published.

- **Consolidation sources:** [automation/bug-scan-20260928-1825](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/d704fa1cea722332c6d82f0aa2d3f9680ac8ae87/bugs.md); [automation/bug-scan-20260929-1421](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/e629428d0573b0b6e3bbf952e77f16495bc60fa7/bugs.md); [automation/bug-scan-20260930-0123](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/be3316ade6780966fd46791ae58d962ccbe0f03f/bugs.md); [Drive: 2026-09-30 01-23 MDT - automation-bug-scan-20260930-0123 - Diff](https://docs.google.com/spreadsheets/d/1_WjxM50XfWlSay0EYe7KKqo0UsDms-ZGMu0yJ7YdWmQ/edit?usp=drivesdk); [Drive: 2026-09-29 1421 MDT - automation-bug-scan-20260929-1421 - Instructions](https://docs.google.com/spreadsheets/d/1PxZ2lsUdx2DcGC4s3qaF4cBQ8agSIvjTCrsJu4LDEws/edit?usp=drivesdk); [Drive: 2026-09-29 1421 MDT - automation-bug-scan-20260929-1421 - Diff](https://docs.google.com/spreadsheets/d/1xPKDnEDkwHqADDFeUzABbYFT6iT-RFeG66EhMkpBByE/edit?usp=drivesdk); [Drive: 2026-09-28 1825 - automation-bug-scan-20260928-1825 - bugs.diff](https://docs.google.com/document/d/13Ixd0HQYso07yZjpKF_kGSw0XRsQVXTqpSpQPaI3JE8/edit?usp=drivesdk); [Drive: 2026-09-28 1825 - automation-bug-scan-20260928-1825 - integration instructions](https://docs.google.com/document/d/1KM1MaWJm_OiNONX7BqGauWCR3EJYlmu9TB6Xi1bzpuc/edit?usp=drivesdk); [Drive: 2026-09-30 01-23 MDT - automation-bug-scan-20260930-0123 - Instructions](https://docs.google.com/spreadsheets/d/1T-xVFt2LWX3ycdaLCcMfdzuq3AVXOovKHKSKdlD0LGg/edit?usp=drivesdk)

### 144. GNSS Stream Diagnostic reports the receiver unavailable when device inventory exceeds 12 entries

- **Status:** Open.

- **Affected code:** `Apps/gnss_stream_diagnostic.c::find_receiver()`; the all-or-nothing `inventory()` contract in `lib/NativeApps/include/T5DeviceApi.h` and `src/native/NativeDeviceBridge.cpp::inventory()`.
- **Trigger / reproduction:** Make more than 12 devices visible in the device registry while a valid `gps-nmea` UART device with `location.position` is available, then launch GNSS Stream Diagnostic.
- **Observed / logically demonstrated failure:** `find_receiver()` allocates exactly 12 `t5_device_info_t` records and calls `inventory(entries, 12, &count)`. The ABI explicitly defines inventory as all-or-nothing: when capacity is below the registry count it returns `T5_DEVICE_LIMIT` and reports the required count. The app treats every result other than `T5_DEVICE_OK` as no receiver and returns handle 0, so a 13th unrelated device makes the UI report `GNSS driver unavailable` even when the GNSS device is healthy.
- **Likely root cause:** The diagnostic assumes a fixed maximum inventory instead of handling the API's documented capacity-negotiation result.
- **Impact:** As more USB, internal, BLE, or other providers register devices, the GNSS diagnostic can become unusable for reasons unrelated to GPS. This can misdiagnose a working receiver/provider as missing and prevents the consent/stream test from running.
- **Repair direction:** Retry using the required count returned with `T5_DEVICE_LIMIT`, using bounded dynamic or PSRAM storage, or provide a filtered provider query. Add a regression with 13 or more devices and place the GNSS device at multiple registry positions to prove discovery remains reliable.

- **Consolidation sources:** [automation/bug-scan-20260928-1825](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/d704fa1cea722332c6d82f0aa2d3f9680ac8ae87/bugs.md); [Drive: 2026-09-28 1825 - automation-bug-scan-20260928-1825 - bugs.diff](https://docs.google.com/document/d/13Ixd0HQYso07yZjpKF_kGSw0XRsQVXTqpSpQPaI3JE8/edit?usp=drivesdk); [Drive: 2026-09-28 1825 - automation-bug-scan-20260928-1825 - integration instructions](https://docs.google.com/document/d/1KM1MaWJm_OiNONX7BqGauWCR3EJYlmu9TB6Xi1bzpuc/edit?usp=drivesdk)

### 145. Risc Strike silently refuses firmware versions its manifest declares compatible

- **Status:** Open.
- **Affected code:** `Apps/risc_strike.c::app_main()`, `Apps/risc_strike.json`, and the append-only `t5_video_api_v1` definition in `lib/NativeApps/include/T5VideoApi.h`.
- **Trigger / reproduction:** Build the current Risc Strike 1.0.2 against the current header, then launch it on firmware 1.3.24 through 1.3.31, which the app manifest explicitly permits. Those firmware versions expose video API v1 before the optional `scan_stats` tail member added in firmware 1.3.32, so their valid v1 struct is shorter than the current header's full struct.
- **Observed / logically demonstrated failure:** `app_main()` rejects the provider when `g_video->struct_size < sizeof(*g_video)` and returns before starting video. Because `sizeof(*g_video)` includes the later optional `scan_stats` callback, an otherwise compatible v1 provider is treated as unusable even though Risc Strike never calls `scan_stats`. The installed app can therefore launch to nothing on firmware versions its own `min_firmware_version: 1.3.24` says are supported.
- **Likely root cause:** The app validates the entire current append-only API layout instead of validating only the required prefix and callbacks it actually consumes.
- **Impact:** Package compatibility metadata is false for a range of accepted firmware releases, and users can install an app that immediately refuses to run.
- **Repair direction:** Replace the full-`sizeof` gate with a `struct_size` check through the last required member, then validate the required callbacks individually. If a post-1.3.24 feature is truly mandatory, raise the manifest minimum instead. Add an ABI-tail regression using a shortened but valid video-v1 provider.

- **Consolidation sources:** [automation/bug-scan-20260928-1926](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/149c4714cf3d6f46d17a6c4691960420368b9fd2/bugs.md)

### 146. Risc Strike cannot fire or attack and renders stale hit effects after about 24.8 days of device uptime

- **Status:** Open.
- **Affected code:** `Apps/risc_strike_engine.inc`, especially `fps_time_reached()`, `fps_reset_game()`, `fps_fire()`, `fps_update_enemies()`, and the muzzle/damage/enemy-hit rendering checks.
- **Trigger / reproduction:** Let the device millisecond clock reach `0x80000000` (2,147,483,648 ms, about 24.85 days of uptime), then start or reset Risc Strike. `fps_reset_game()` initializes `g_next_fire_ms`, `g_muzzle_until_ms`, `g_damage_until_ms`, and every enemy's attack/hit deadlines to zero.
- **Observed / logically demonstrated failure:** `fps_time_reached(now, 0)` casts `now - 0` to signed 32-bit. For `now` in `0x80000000..0xffffffff`, that value is negative, so the zero sentinel is treated as a future deadline. `fps_fire()` refuses every shot, enemies fail their initial attack-ready test, and the rendering checks interpret zero-valued muzzle/damage/hit deadlines as still active. The state remains wrong until the 32-bit millisecond clock wraps near 49.7 days.
- **Likely root cause:** A zero sentinel meaning "inactive/immediately ready" is mixed with the signed half-range wraparound comparison, whose validity assumes the compared deadline is a real nearby timestamp.
- **Impact:** A long-running device can start Risc Strike in a materially broken game state: shooting and enemy attacks are disabled while transient hit/damage visuals appear continuously.
- **Repair direction:** Represent inactive timers explicitly, or use separate helpers that treat an inactive deadline as ready for cooldown/attack checks and expired for visual-effect checks while preserving wrap-safe comparison for real deadlines. Add tests around `0x7fffffff`, `0x80000000`, `0xffffffff`, and wrap to zero.

- **Consolidation sources:** [automation/bug-scan-20260928-1926](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/149c4714cf3d6f46d17a6c4691960420368b9fd2/bugs.md)

### 147. Hollow Trail can submit an old-chapter frame when a level transition happens inside a render checkpoint

- **Status:** Open.
- **Affected code:** `Apps/hollow_trail.c::ht_input_update()`, `ht_advance()`, and `app_main()`; `Apps/hollow_trail_engine.inc::ht_render_scene()` and its cooperative `ht_checkpoint()` / `ht_render_service()` calls.
- **Trigger / reproduction:** Reach a solved chapter goal while movement is held, with a redraw due and no already-prepared frame. Arrange for the next fixed simulation step to cross the goal during one of `ht_render_scene()`'s input checkpoints; production deliberately services input during raster work.
- **Observed / logically demonstrated failure:** The renderer snapshots the old `ht_game` and continues drawing that chapter while a checkpoint can advance live physics, increment `ht.level`, call `ht_spawn(true)`, and increment `scene_revision`. After `ht_render_scene()` returns, `app_main()` still marks those old pixels `prepared=true`; it checks only quit/debug-jump before the normal submit path. If the video service can accept a frame, the old-chapter buffer is packed and submitted. The existing `ht.level != ht_geometry_level` branch that says it discards an old-level prepared frame runs only at the top of the next loop, after that stale frame may already have been scanned.
- **Likely root cause:** Prepared-frame validity is fenced only before rendering and at the next host-loop iteration, not after cooperative render checkpoints that are allowed to mutate the live simulation and chapter.
- **Impact:** Chapter transitions can visibly flash or ghost a full stale frame from the previous level on the e-paper display, defeating the loading/old-frame discard logic.
- **Repair direction:** Capture a render generation/level epoch before raster work and revalidate it after rendering, after packing, and immediately before submit; discard the prepared buffer whenever the live level or generation changed. Add a host regression that forces a goal-crossing simulation step from a render checkpoint and asserts no old-level submit occurs.

- **Consolidation sources:** [automation/bug-scan-20260928-1926](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/149c4714cf3d6f46d17a6c4691960420368b9fd2/bugs.md)

### 148. File-association rebuild silently truncates the registry after 128 handlers and skips later applications

- **Status:** Open.
- **Affected code:** `src/native/FileAssociationRegistry.h::kMaxHandlers`; `src/native/FileAssociationRegistry.cpp::addHandler()`, `scanCanonicalApp()`, and `rebuild()`; app-manifest file-type parsing in `src/native/AppManifest.h`.
- **Trigger / reproduction:** Install enough valid applications with declared `file_types` to exceed 128 total associations. The registry adds five system-reader handlers first, and each application manifest may declare up to 12 file types, so eleven 12-type applications are sufficient to overflow the table. Put another valid file-handling application later in `/Apps`, rebuild associations, then ask File Browser/file-open APIs for a type handled only by the later application.
- **Observed / logically demonstrated failure:** `addHandler()` returns `false` once `handlers.size() >= kMaxHandlers`, but `rebuild()` treats that capacity as a normal stopping condition: its top-level `/Apps` scan runs only while `handlers.size() < kMaxHandlers`. Once the 128th handler is accepted, remaining applications are never scanned. If the limit is reached partway through one manifest, earlier file types from that app can be retained while later types are silently dropped. The truncated vector is then sorted, marked `loaded = true`, persisted, and `rebuild()` returns success. This is distinct from bug #23, which is the File Browser UI exposing only eight already-registered handlers for one file; here the missing handlers never enter the registry at all.
- **Likely root cause:** A fixed storage bound is being used as successful end-of-enumeration rather than as an explicit overflow/error or a paging boundary.
- **Impact:** Installed applications and valid file associations become nondeterministically unavailable based on `/Apps` enumeration order and the number of associations declared by unrelated apps. Open-with resolution, file handoff, and the persisted generated association index can all omit otherwise valid handlers while reporting a successful rebuild.
- **Repair direction:** Make the registry dynamically sized/PSRAM-backed or build it in bounded pages without stopping the application scan. If a hard policy limit remains, detect overflow explicitly, refuse to publish/persist a partial registry, and surface a diagnostic. Add coverage with more than 128 associations, including an app whose 12 types straddle the boundary and a later app that owns a unique extension.

- **Consolidation sources:** [automation/bug-scan-20260928-1954](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/463d4a88b152837d01dd5dc0b7bf5f57cd4a8a2b/bugs.md); [Drive: 2026-09-28 1954 MDT - automation-bug-scan-20260928-1954 - Diff](https://docs.google.com/document/d/1gVvYNEFq8c5h6nsM-zqqxCxgLAnZ9hDKUCHxjKf-jqw/edit?usp=drivesdk); [Drive: 2026-09-28 1954 MDT - automation-bug-scan-20260928-1954 - Instructions](https://docs.google.com/document/d/1BUHCnlpsqv8OMCTVvEuJbEbNvNhaP8C-xhhCYnt_ngI/edit?usp=drivesdk)

### 149. Wi-Fi load failures leave stale credentials live and automatic networking can reuse them

- **Status:** Open.
- **Affected code:** `src/WifiCredentialStore.cpp::loadFromFile()`; `src/JsonSettingsIO.cpp::loadWifi()`; callers that ignore the load result, including `src/activities/network/WifiSelectionActivity.cpp::onEnter()`, `src/runtime/network/SavedNetworkConnection.cpp::ensureSavedConnection()`, and `src/native/NativeOtaBridge.cpp::ensureOtaNetworkReady()`.
- **Trigger / reproduction:** During one firmware session, first load a valid `/.crosspoint/wifi.json` so the singleton contains a last-connected SSID and credentials. Then replace/truncate that file with non-empty malformed JSON (or inject an SD read that produces malformed JSON) and invoke Wi-Fi selection, an automatic saved-network connection, or firmware-update network bootstrap.
- **Observed / logically demonstrated failure:** `JsonSettingsIO::loadWifi()` returns `false` immediately when `deserializeJson()` fails, before clearing or replacing `store.lastConnectedSsid` and `store.credentials`. `WifiCredentialStore::loadFromFile()` propagates that failure, but the listed callers discard the return value and immediately consult the singleton. They can therefore select and submit the credentials from the previous successful load even though the current durable credential file is unreadable. The Wi-Fi selector can also label those stale entries as saved networks. This is separate from the earlier schema-validation finding: the demonstrated state here is a parse/read failure retaining an older live credential set, not a syntactically valid file being accepted as an empty store.
- **Likely root cause:** Wi-Fi deserialization mutates a long-lived singleton in place and does not define failure as an invalidation boundary; consumers assume that calling `loadFromFile()` makes the in-memory state authoritative regardless of its returned status.
- **Impact:** Automatic networking can connect with credentials the current SD state no longer supplies, making recovery behavior misleading and potentially using an old password/SSID after corruption or external credential replacement. A transient storage fault can therefore affect later network choice rather than failing closed for that attempt.
- **Repair direction:** Parse into temporary credential/last-SSID state and publish it only after complete validation; on load failure, callers that need persisted credentials must not initiate a new connection from previously cached values. Preserve an already-established network separately from credential loading. Add regressions for valid-load → malformed-file → selector/auto-connect and for cold boot with malformed JSON.

- **Consolidation sources:** [automation/bug-scan-20260928-1954](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/463d4a88b152837d01dd5dc0b7bf5f57cd4a8a2b/bugs.md); [automation/bug-scan-20260929-2120](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/82bef6c8270ad0ed072369ee19b07022dc82e181/bugs.md); [Drive: 2026-09-28 1954 MDT - automation-bug-scan-20260928-1954 - Diff](https://docs.google.com/document/d/1gVvYNEFq8c5h6nsM-zqqxCxgLAnZ9hDKUCHxjKf-jqw/edit?usp=drivesdk); [Drive: 2026-09-29 2120 MDT - automation-bug-scan-20260929-2120 - Instructions](https://docs.google.com/spreadsheets/d/1uI1Z04FNBkqdLQxhgZuCSZXUFQbOTUOFJ7dzYn39FoE/edit?usp=drivesdk); [Drive: 2026-09-29 2120 MDT - automation-bug-scan-20260929-2120 - Instructions](https://docs.google.com/document/d/1xv-O64ISDIO8zYQ7r6A9vc7x-RMMWrApoVqtPFzcB1U/edit?usp=drivesdk); [Drive: 2026-09-28 1954 MDT - automation-bug-scan-20260928-1954 - Instructions](https://docs.google.com/document/d/1BUHCnlpsqv8OMCTVvEuJbEbNvNhaP8C-xhhCYnt_ngI/edit?usp=drivesdk); [Drive: 2026-09-29 2120 MDT - automation-bug-scan-20260929-2120 - Diff](https://docs.google.com/spreadsheets/d/1v0enkcChAVBR3-RFLlrRcIS8MhmEQyj1AV0LlBx-aj4/edit?usp=drivesdk); [Drive: 2026-09-29 2120 MDT - automation-bug-scan-20260929-2120 - Diff](https://docs.google.com/spreadsheets/d/1Dq6s7qm3fIlKrMX_AVJaMlTG3V-Vfhf-Onk_qRqtUog/edit?usp=drivesdk); [Drive: 2026-09-29 2120 MDT - automation-bug-scan-20260929-2120 - Diff](https://docs.google.com/document/d/1Igvgxgg6gX8_I8-QVjW_Ve2n06-Dp58Adt6XIthpikI/edit?usp=drivesdk)

### 150. EPUB manifest property matching treats prefixed properties beginning with "nav" as the navigation document

- **Status:** Open.
- **Affected code:** `lib/Epub/Epub/parsers/ContentOpfParser.cpp::startElement()`, manifest `item` handling for the `properties` attribute (and the analogous `cover-image` check).
- **Trigger / reproduction:** Create a valid EPUB 3 package that declares an extension prefix such as `navx:` and lists an ordinary XHTML manifest item before the real navigation item with `properties="scripted navx:aux"`. Give a later item the actual reserved property `properties="nav"`, then open the book and its table of contents.
- **Observed / logically demonstrated failure:** The parser recognizes navigation with `properties == "nav" || properties.find("nav ") == 0 || properties.find(" nav") != npos`. The final substring test accepts any whitespace-delimited property whose text merely starts with `nav`, including the valid prefixed property `navx:aux`. That first false match populates `tocNavPath`; because later detection runs only while `tocNavPath.empty()`, the real `nav` item is ignored. TOC parsing is then directed at the unrelated XHTML resource. The `cover-image` path uses the same prefix-substring pattern and can misclassify a similarly named extension property.
- **Likely root cause:** A space-separated token list is being searched with prefix substrings instead of tokenized and compared for exact reserved-property equality.
- **Impact:** Standards-valid EPUBs using extension vocabularies whose property tokens begin with a reserved property name can lose or corrupt navigation/cover discovery despite containing the correct resources.
- **Repair direction:** Tokenize `properties` on XML whitespace and compare complete tokens exactly to `nav` and `cover-image`; do not treat prefixed extension properties as reserved terms. Add fixtures for exact `nav`, multiple properties in different positions, `navx:aux`, exact `cover-image`, and similarly prefixed non-reserved properties.

- **Consolidation sources:** [automation/bug-scan-20260928-1954](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/463d4a88b152837d01dd5dc0b7bf5f57cd4a8a2b/bugs.md); [Drive: 2026-09-28 1954 MDT - automation-bug-scan-20260928-1954 - Diff](https://docs.google.com/document/d/1gVvYNEFq8c5h6nsM-zqqxCxgLAnZ9hDKUCHxjKf-jqw/edit?usp=drivesdk); [Drive: 2026-09-28 1954 MDT - automation-bug-scan-20260928-1954 - Instructions](https://docs.google.com/document/d/1BUHCnlpsqv8OMCTVvEuJbEbNvNhaP8C-xhhCYnt_ngI/edit?usp=drivesdk)

### 151. EPUB page QR generation selects versions by alphanumeric capacity and rejects valid byte-mode page text

- **Status:** Open.
- **Affected code:** `src/util/QrUtils.cpp::drawQrCode()`; `src/activities/reader/EpubReaderActivity.cpp` Display QR action; `src/activities/reader/QrDisplayActivity.cpp::render()`.
- **Trigger / reproduction:** Open an EPUB page whose extracted text contains byte-mode characters (ordinary lowercase prose is sufficient) and is 79–114 UTF-8 bytes, then choose Display QR. Equivalent failing ranges are 272–395, 859–1066, and 1733–2110 bytes.
- **Observed / logically demonstrated failure:** `drawQrCode()` selects versions using 114→v4, 395→v10, 1066→v20, and 2110→v30, which are the QR library's alphanumeric ECC_LOW capacities. `qrcode_initText()` uses byte mode for lowercase/non-alphanumeric text; those versions hold only 78, 271, 858, and 1732 bytes respectively. Normal prose in the gap makes `qrcode_initText()` fail, so the activity renders no QR code even though a larger supported version could encode it.
- **Likely root cause:** Version selection uses capacity thresholds for the wrong QR encoding mode and does not retry with a larger version after encoding failure.
- **Impact:** Display QR deterministically fails for substantial ranges of valid EPUB page text, including ordinary lowercase prose.
- **Repair direction:** Select capacity using the actual encoding mode or try progressively larger versions until encoding succeeds. Keep the version-40 cap and UTF-8-safe truncation. Test byte-mode boundaries 78/79, 271/272, 858/859, and 1732/1733 plus alphanumeric-only text.

- **Consolidation sources:** [automation/bug-scan-20260928-2026](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/fa556b486d37cd6260a5e0f5e49b500af7b6bb4e/bugs.md); [automation/bug-scan-20260929-2226](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/13108f6ab843969240b686a9aa624cfb63470260/bugs.md); [Drive: 2026-09-28 2026 - automation-bug-scan-20260928-2026 - Diff](https://docs.google.com/spreadsheets/d/136xrqFzGI3G_pT1iH4w8DrZ_dZPG38a4OAtRKaR5Eyc/edit?usp=drivesdk); [Drive: 2026-09-28 2026 - bug-scan-20260928-2026 - Instructions](https://docs.google.com/spreadsheets/d/1CZ2WLDkqQC3yCB2EmFcrOaOJtEkdUlRhqPACOCb1Pnw/edit?usp=drivesdk); [Drive: 2026-09-29 2226 MDT - automation-bug-scan-20260929-2226 - 13108f6 - bugs.md diff](https://docs.google.com/spreadsheets/d/1XIr2_53lXbldoungfg66A_4OCh1SPVNtyG4xYoOIZaY/edit?usp=drivesdk); [Drive: 2026-09-29 2226 MDT - automation-bug-scan-20260929-2226 - 13108f6 - integration instructions](https://docs.google.com/spreadsheets/d/1GX7YqdYIr7lUAV1r91jtVHJFe7M79rteWrEQngf2J68/edit?usp=drivesdk)

### 152. Web file-manager rename and move leave EPUB path-indexed state attached to the old filename

- **Status:** Open.
- **Affected code:** `src/network/CrossPointWebServer.cpp::handleRename()` and `handleMove()`; path-indexed state in `RecentBooksStore`, `CrossPointState::openEpubPath`, and bookmark storage through `src/util/BookmarkUtil.cpp`.
- **Trigger / reproduction:** Read and bookmark an EPUB so Recent Books/progress and bookmarks exist. In File Transfer's web file manager, rename the EPUB or move it to another directory, then return to Recent Books and open the new path.
- **Observed / logically demonstrated failure:** Both web handlers clear the old EPUB cache and rename the filesystem object, but neither migrates path-indexed reader metadata after success. Recent Books/open-book state still reference the old path and bookmark sidecar naming is derived from the book path, so the moved book appears to lose its bookmarks and stale entries remain.
- **Likely root cause:** The web file manager treats an EPUB as an isolated file instead of invoking a shared transactional book-path migration operation.
- **Impact:** Routine web rename/move can strand bookmarks, break Recent Books entries, and leave the retained open-book path stale while reporting success.
- **Repair direction:** After a successful filesystem rename, transactionally migrate Recent Books, retained/open path, bookmarks, and other path-derived sidecars, with rollback/error handling. Centralize this behavior and test rename plus cross-directory move with bookmarks and recent-state present.

- **Consolidation sources:** [automation/bug-scan-20260928-2026](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/fa556b486d37cd6260a5e0f5e49b500af7b6bb4e/bugs.md); [Drive: 2026-09-28 2026 - automation-bug-scan-20260928-2026 - Diff](https://docs.google.com/spreadsheets/d/136xrqFzGI3G_pT1iH4w8DrZ_dZPG38a4OAtRKaR5Eyc/edit?usp=drivesdk); [Drive: 2026-09-28 2026 - bug-scan-20260928-2026 - Instructions](https://docs.google.com/spreadsheets/d/1CZ2WLDkqQC3yCB2EmFcrOaOJtEkdUlRhqPACOCb1Pnw/edit?usp=drivesdk)

### 153. Screenshot saving reports success even when the final SD close/commit fails

- **Status:** Open.
- **Affected code:** `src/util/ScreenshotUtil.cpp::saveFramebufferAsBmp()` and `ScreenshotUtil::takeScreenshot()`.
- **Trigger / reproduction:** Take a screenshot while fault-injecting an SD/filesystem failure on the final BMP close/flush after all header and row writes returned their requested lengths.
- **Observed / logically demonstrated failure:** The function checks header/row write lengths, then calls `file.close()` and discards its boolean result. A close-time commit failure therefore leaves `write_error` false and returns success; `takeScreenshot()` logs "Screenshot saved" and flashes the success border even though the file was not successfully committed and may be invalid.
- **Likely root cause:** The write loop is treated as the durability boundary and close/flush is omitted from the success condition; the file is also written directly to its final pathname.
- **Impact:** Storage faults can silently create corrupt/incomplete screenshots while the UI confirms success.
- **Repair direction:** Require successful close/sync, remove failed output, and preferably stage to a sibling temporary file and atomically rename only after close plus expected-size verification. Add a close-failure test proving no success indication and no corrupt final file.

- **Consolidation sources:** [automation/bug-scan-20260928-2026](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/fa556b486d37cd6260a5e0f5e49b500af7b6bb4e/bugs.md); [automation/bug-scan-20260930-0022](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/2b932092e31b6d6b85bd45c7ada99cb728994e26/bugs.md); [Drive: 2026-09-30 0022 MDT - automation-bug-scan-20260930-0022 - Diff](https://docs.google.com/spreadsheets/d/1VGiMSPAY900zqoow0Q_kZG_D-96pFkos_O65zZAw_mo/edit?usp=drivesdk); [Drive: 2026-09-28 2026 - automation-bug-scan-20260928-2026 - Diff](https://docs.google.com/spreadsheets/d/136xrqFzGI3G_pT1iH4w8DrZ_dZPG38a4OAtRKaR5Eyc/edit?usp=drivesdk); [Drive: 2026-09-28 2026 - bug-scan-20260928-2026 - Instructions](https://docs.google.com/spreadsheets/d/1CZ2WLDkqQC3yCB2EmFcrOaOJtEkdUlRhqPACOCb1Pnw/edit?usp=drivesdk); [Drive: 2026-09-30 0022 MDT - automation-bug-scan-20260930-0022 - Instructions](https://docs.google.com/spreadsheets/d/1hniZfzD9J_jLioHAEqaS74cAsknU2Ri9qm5BXSs-J9Y/edit?usp=drivesdk)

### 154. Time Card keeps failed punch edits live and can persist them on a later successful save

- **Status:** Open.
- **Affected code:** `Apps/timecard.c`, especially `ensure_day()`, `set_punch()`, `punch_today()`, and `consume_keyboard()`.
- **Trigger / reproduction:** Start with a valid Time Card store, then force `storage->write_file_atomic()` to fail for one clock punch or manual edit. Leave the app running, allow storage writes to recover, and make a later punch/edit that saves successfully. Also exercise the same failure while `day_count == MAX_DAYS` and the failed operation creates a new date.
- **Observed / logically demonstrated failure:** `set_punch()` mutates the live `days[]` model before calling `save_store()` and does not restore that mutation when persistence fails. `punch_today()` reports **Could not save punch**, and the manual-edit path reports failure, but the changed punch remains in memory. A later successful `save_store()` serializes that previously failed change along with the new one. At the 400-day limit, `ensure_day()` can also evict the oldest in-memory day before the failed save, and that eviction can become durable on the next successful write.
- **Likely root cause:** The Time Card write path uses mutate-then-persist semantics without a snapshot, staged model, or rollback on persistence failure.
- **Impact:** A punch/edit that the UI explicitly reports as unsaved can later become durable without the user's knowledge. At capacity, a failed operation can also prime an older valid day for later deletion.
- **Repair direction:** Make punch updates transactional. Stage the candidate day array/count (including any capacity eviction), persist the staged representation, and publish it to the live model only after the write succeeds; alternatively snapshot and restore every affected element/count on failure. Add fault-injection tests proving a failed existing-day edit, failed new-day insertion, and failed insertion at `MAX_DAYS` leave both live and durable history unchanged after subsequent successful saves.

- **Consolidation sources:** [automation/bug-scan-20260928-2119](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/56735b0d346c8773e8fadfe71a2472a7af84a7a9/bugs.md); [Drive: 2026-09-28 2119 - automation_bug-scan-20260928-2119 - Diff](https://docs.google.com/spreadsheets/d/175TW_3ERYf9Jt3bf75XoP4ztFpnvjNwOXaLbGBnBqn8/edit?usp=drivesdk); [Drive: 2026-09-28 2119 - automation_bug-scan-20260928-2119 - Instructions](https://docs.google.com/spreadsheets/d/1jF_5dBTG4kFTfagdowLbReigaZpKvRBVhT9sWytFJhY/edit?usp=drivesdk)

### 155. KOReader document matching toggles repeatedly from one held Confirm press

- **Status:** Open.
- **Affected code:** `Apps/koreader_sync.c`, `activate_selected()` and the main `app_main()` input loop; the level-triggered button contract exported by `src/native/NativeAppHost.cpp::poll()`.
- **Trigger / reproduction:** Open **KOReader Sync**, select **Document Matching**, then press and hold Confirm for longer than one 50 ms polling interval.
- **Observed / logically demonstrated failure:** The app tests `input.buttons & T5_APP_BUTTON_CONFIRM` on every raw app poll and calls `activate_selected()` each time the bit remains asserted. For the Document Matching row, every call flips Filename/Binary and persists the new value. One physical hold can therefore toggle and write the setting several times, with the final mode determined by release timing rather than one deliberate activation.
- **Likely root cause:** KOReader Sync treats the native app ABI's level-triggered button state as a one-shot event and does not gate Confirm on a rising edge or release.
- **Impact:** A user can release Confirm with the opposite matching mode from the one they intended, while one press causes multiple unnecessary settings writes.
- **Repair direction:** Consume edge-based UI events for this screen or track the previous button mask and activate only on the Confirm rising edge. Add a regression that keeps Confirm asserted across several polls and verifies exactly one match-method change and one persistence attempt.

- **Consolidation sources:** [automation/bug-scan-20260928-2119](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/56735b0d346c8773e8fadfe71a2472a7af84a7a9/bugs.md); [Drive: 2026-09-28 2119 - automation_bug-scan-20260928-2119 - Diff](https://docs.google.com/spreadsheets/d/175TW_3ERYf9Jt3bf75XoP4ztFpnvjNwOXaLbGBnBqn8/edit?usp=drivesdk); [Drive: 2026-09-28 2119 - automation_bug-scan-20260928-2119 - Instructions](https://docs.google.com/spreadsheets/d/1jF_5dBTG4kFTfagdowLbReigaZpKvRBVhT9sWytFJhY/edit?usp=drivesdk)

### 156. Rom Manager silently truncates Vimm browse/search results after 96 entries

- **Status:** Open.
- **Affected code:** `Apps/rom_manager.c`, `MAX_VIMM`, `fetch_vimm_url()`, `add_vimm_entry()`, `fetch_vimm()`, `search_vimm()`, and `open_vimm_page()`.
- **Trigger / reproduction:** Load a Vimm browse, letter, or search response containing more than 96 valid navigation/game entries. On the top-level browse response, page/navigation links and games share the same 96-entry array, so enough navigation entries reduce the number of game rows that can be retained even further.
- **Observed / logically demonstrated failure:** Both parsing loops in `fetch_vimm_url()` stop when `vimm_count == MAX_VIMM`. The function still returns success, exposes only the retained prefix, and provides no continuation cursor, next page, or truncation indication. Because navigation entries are added before game rows when `include_pages` is true, they consume the same fixed capacity and can make valid games disappear from the very response that successfully loaded them.
- **Likely root cause:** A fixed render/result buffer is also being used as the complete remote catalog model, with no pagination state or overflow contract.
- **Impact:** Valid Game Boy catalog titles beyond the retained prefix cannot be discovered or opened/installed through Rom Manager, and the visible result set can vary with unrelated navigation-link count.
- **Repair direction:** Separate navigation from title storage and page/stream remote results instead of treating `MAX_VIMM` as the catalog size. Preserve a continuation/page cursor or virtualize rows over parsed results, and visibly report truncation if the remote source cannot be paged. Add tests with 96, 97, and substantially larger result sets, including a top-level response where navigation entries plus games exceed 96.

- **Consolidation sources:** [automation/bug-scan-20260928-2119](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/56735b0d346c8773e8fadfe71a2472a7af84a7a9/bugs.md); [Drive: 2026-09-28 2119 - automation_bug-scan-20260928-2119 - Diff](https://docs.google.com/spreadsheets/d/175TW_3ERYf9Jt3bf75XoP4ztFpnvjNwOXaLbGBnBqn8/edit?usp=drivesdk); [Drive: 2026-09-28 2119 - automation_bug-scan-20260928-2119 - Instructions](https://docs.google.com/spreadsheets/d/1jF_5dBTG4kFTfagdowLbReigaZpKvRBVhT9sWytFJhY/edit?usp=drivesdk)

### 157. TXT/Markdown paging treats short positive SD reads as complete chunks and parses unread heap bytes

- **Status:** Open.
- **Affected code:** `lib/Txt/Txt.cpp`, `Txt::readContent()`; `src/activities/reader/TxtReaderPaging.cpp`, `TxtReaderActivity::loadPageAtOffset()` and `peekMarkdownLine()`.
- **Trigger / reproduction:** Open a TXT or Markdown document, then make an SD read return a short positive count without satisfying the requested read length (for example, fault-inject a partial read, or truncate/replace the file after `Txt::load()` cached its original size). In `loadPageAtOffset()`, request an 8 KiB chunk where `FsFile::read()` returns between 1 and 8191 bytes.
- **Observed / logically demonstrated failure:** `Txt::readContent()` returns `true` for any `bytesRead > 0` and does not return the actual byte count. Its callers allocate an uninitialized buffer sized for the full requested chunk and, after that boolean success, parse all `chunkSize` bytes. `loadPageAtOffset()` therefore treats unread heap bytes as document text and can advance `nextOffset` across bytes that were never read; page-index construction can then persist those bogus offsets. `peekMarkdownLine()` has the same assumption for its 256-byte buffer.
- **Likely root cause:** The storage helper collapses “some bytes read” and “the requested range was read completely” into the same boolean result, while paging code assumes an exact-length read.
- **Impact:** A transient or concurrent file-read short read can display garbage or stale heap contents as book text, skip real document bytes, corrupt the persisted page index, and make subsequent navigation inconsistent until the cache is rebuilt.
- **Repair direction:** Return the actual count from `readContent()` or require `bytesRead == length` for this exact-range API. Callers must parse only initialized bytes and treat a short read before the cached EOF as an I/O/change error. Add fault-injection tests for 1-byte, mid-chunk, and EOF-adjacent short reads plus a file-size change after `Txt::load()`.

- **Consolidation sources:** [automation/bug-scan-20260928-2221](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/6885f8bc3523e0f88382ec0b973d7fe3fc756b29/bugs.md); [automation/bug-scan-20260929-0923](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/61e0308885e3d82d10013b44c0cc25885984bb8c/bugs.md); [Drive: 2026-09-29 09-23 MDT - automation-bug-scan-20260929-0923 - Instructions](https://docs.google.com/document/d/16-a2i6BOru1O8pdLvMa6KMQ7DHpzVTz_T5ZLxhspYIw/edit?usp=drivesdk); [Drive: 2026-09-28 2221 - automation-bug-scan-20260928-2221 - Diff](https://docs.google.com/document/d/1WAu1zlTwHP02PAL8dRZCc43rWsZRMTtkqiKkih4QDDs/edit?usp=drivesdk); [Drive: 2026-09-28 2221 - automation-bug-scan-20260928-2221 - Instructions](https://docs.google.com/document/d/1sbRBPO6bTUEqImKAaQtAV3GrwfhQqg6demvhIOnReJc/edit?usp=drivesdk); [Drive: 2026-09-29 09-23 MDT - automation-bug-scan-20260929-0923 - Diff](https://docs.google.com/document/d/1blE03USfMDKdOZCAsm-rzhy6EvDcdVGIFZ6T6cmGxjU/edit?usp=drivesdk)

### 158. XTC rendering uses the first page's dimensions for every page and can render uninitialized memory

- **Status:** Open.
- **Affected code:** `lib/Xtc/Xtc/XtcParser.cpp`, `readFirstPageInfo()`, `readPageTableEntry()`, and `loadPage()`; `lib/Xtc/Xtc.cpp`, `getPageWidth()` / `getPageHeight()`; `src/activities/reader/XtcReaderActivity.cpp`, `renderPage()`.
- **Trigger / reproduction:** Use an otherwise accepted multi-page XTC/XTCH whose first page is 480x800 and whose later page header is 400x800, with valid magic and 40,000 bytes of 1-bit bitmap data for that later page. Navigate to the later page. The inverse case, such as a 600x800 later page, demonstrates the corresponding false “buffer too small” failure.
- **Observed / logically demonstrated failure:** `readFirstPageInfo()` stores only the first table entry's dimensions as the parser-wide defaults. Although every `PageTableEntry` carries its own width and height, `Xtc::getPageWidth()` / `getPageHeight()` expose only those first-page defaults, so `XtcReaderActivity::renderPage()` allocates and iterates a 480x800 buffer for every page. `XtcParser::loadPage()`, however, computes the bytes to read from the current page header. In the 400x800 case it reads 40,000 bytes successfully into a 48,000-byte allocation, then the renderer indexes all 48,000 bytes using the first-page 60-byte row stride, consuming 8,000 bytes that were never initialized by the page load. A larger later page instead fails because the first-page-sized allocation is too small.
- **Likely root cause:** Per-page geometry exists in the format and parser but the public reader interface treats geometry as book-global, and no invariant check requires later table/header dimensions to equal the first page.
- **Impact:** Dimension-varying or corrupted XTC files can show heap garbage/stale data on the display, render pages with the wrong stride, or reject later pages that contain otherwise readable bitmap data. This is separate from the previously reported non-byte-aligned XTCH plane-size bug because aligned heights and ordinary 1-bit XTC reproduce it.
- **Repair direction:** Resolve and validate the current page's table/header geometry before allocation/rendering. Either expose per-page `PageInfo` to `XtcReaderActivity` and allocate/render from it, or explicitly reject files whose later geometry differs from the supported fixed geometry. Also verify table-entry dimensions/data size against the page header and initialize any buffer region not filled. Add 480→400, 480→600, and table/header-mismatch regression fixtures.

- **Consolidation sources:** [automation/bug-scan-20260928-2221](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/6885f8bc3523e0f88382ec0b973d7fe3fc756b29/bugs.md); [automation/bug-scan-20260929-0923](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/61e0308885e3d82d10013b44c0cc25885984bb8c/bugs.md); [Drive: 2026-09-29 09-23 MDT - automation-bug-scan-20260929-0923 - Instructions](https://docs.google.com/document/d/16-a2i6BOru1O8pdLvMa6KMQ7DHpzVTz_T5ZLxhspYIw/edit?usp=drivesdk); [Drive: 2026-09-28 2221 - automation-bug-scan-20260928-2221 - Diff](https://docs.google.com/document/d/1WAu1zlTwHP02PAL8dRZCc43rWsZRMTtkqiKkih4QDDs/edit?usp=drivesdk); [Drive: 2026-09-28 2221 - automation-bug-scan-20260928-2221 - Instructions](https://docs.google.com/document/d/1sbRBPO6bTUEqImKAaQtAV3GrwfhQqg6demvhIOnReJc/edit?usp=drivesdk); [Drive: 2026-09-29 09-23 MDT - automation-bug-scan-20260929-0923 - Diff](https://docs.google.com/document/d/1blE03USfMDKdOZCAsm-rzhy6EvDcdVGIFZ6T6cmGxjU/edit?usp=drivesdk)

### 159. OPDS XML parsing mistakes extension element names that merely begin with Atom names for real feed elements

- **Status:** Open.
- **Affected code:** `lib/OpdsParser/OpdsParser.cpp`, `OpdsParser::startElement()` and `endElement()`.
- **Trigger / reproduction:** Serve a valid Atom/OPDS entry containing a namespaced extension element such as `<ext:entry-extra>metadata</ext:entry-extra>` before the entry's acquisition `<link>`, or `<ext:title-extra>auxiliary</ext:title-extra>` inside an entry. Namespace declarations can bind `ext` to any extension namespace.
- **Observed / logically demonstrated failure:** The parser recognizes qualified names with tests such as `strstr(name, ":entry") != nullptr`, and uses the same substring pattern for `:link`, `:title`, `:author`, `:name`, and `:id`. Therefore `ext:entry-extra` is treated exactly like Atom `entry`: `startElement()` resets `currentEntry`, and `endElement()` can leave `inEntry` false so the legitimate link/title that follows is ignored. Likewise an extension local name beginning with `title` can overwrite the actual title. These are valid distinct XML qualified names, not aliases for the Atom elements.
- **Likely root cause:** Namespace tolerance was implemented as substring matching instead of comparing the exact local name after an optional namespace prefix.
- **Impact:** OPDS feeds that use otherwise valid extension elements can silently lose books, navigation entries, titles, IDs, or links even though the XML parses successfully.
- **Repair direction:** Compare exact local names: strip an optional QName prefix (or enable Expat namespace processing) and require equality with `entry`, `link`, `title`, `author`, `name`, or `id`. Preserve namespace distinctions where semantics require them. Add fixtures containing `ext:entry-extra`, `ext:title-extra`, and genuinely prefixed Atom elements to prove extensions no longer perturb parser state.

- **Consolidation sources:** [automation/bug-scan-20260928-2221](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/6885f8bc3523e0f88382ec0b973d7fe3fc756b29/bugs.md); [Drive: 2026-09-28 2221 - automation-bug-scan-20260928-2221 - Diff](https://docs.google.com/document/d/1WAu1zlTwHP02PAL8dRZCc43rWsZRMTtkqiKkih4QDDs/edit?usp=drivesdk); [Drive: 2026-09-28 2221 - automation-bug-scan-20260928-2221 - Instructions](https://docs.google.com/document/d/1sbRBPO6bTUEqImKAaQtAV3GrwfhQqg6demvhIOnReJc/edit?usp=drivesdk)

### 160. Hollow Trail's aligned journal framebuffer writes past its PSRAM allocation

- **Status:** Open.

- **Affected code:** `Apps/hollow_trail.c::app_main()` and `HT_PACKED_BYTES`; `Apps/hollow_trail_engine.inc::HT_MEMORY`; full-frame journal writers in `Apps/hollow_trail_journal.inc::ht_journal_frame()` and `ht_journal_render()`.
- **Trigger / reproduction:** Launch Hollow Trail and open/render the in-game journal. For a deterministic bounds test, substitute a guarded allocation for `app->psram_alloc(HT_MEMORY + HT_PACKED_BYTES + 31u)`, exercise allocator-valid base alignments, and render a journal page.
- **Observed / logically demonstrated failure:** The app allocates `HT_MEMORY + HT_PACKED_BYTES + 31` bytes, aligns the engine workspace from `memory`, but independently computes `ht_reader_bitmap = align16(memory + HT_MEMORY + 31)`. With current constants, `HT_MEMORY == 3,161,632` and `HT_PACKED_BYTES == 64,800`, both multiples of 16. The second alignment can therefore move the 64,800-byte journal bitmap beyond the allocation; with a 16-byte-aligned block the final byte is one byte out of bounds, and other ordinary heap alignments can overrun farther. `ht_journal_frame()` and its fallback path call `ht_pack_mono(ht_reader_bitmap, 120)`, which writes the complete packed frame.
- **Likely root cause:** Alignment slack was budgeted as one trailing `+31` region, but the reader bitmap is aligned from `memory + HT_MEMORY + 31` instead of being derived from the already-aligned workspace end or reserving enough additional slack.
- **Impact:** Opening journal/story pages can corrupt adjacent PSRAM allocation contents or allocator metadata, causing delayed rendering corruption, crashes, or unrelated failures.
- **Repair direction:** Derive one aligned workspace base first and place the packed journal buffer immediately after its bounded `HT_MEMORY` region, or explicitly calculate and allocate the worst-case padding for every aligned subregion. Add a guard-byte test over all relevant base-address residues. Open PR #295 changes Hollow Trail rendering but does not modify this allocation or the `HT_MEMORY`/`HT_PACKED_BYTES` placement.

- **Consolidation sources:** [automation/bug-scan-20260928-2327](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/599139f74e27c778eacc465b89a277091e7f1069/bugs.md)

### 161. Web file uploads delete an existing file before the replacement upload is complete

- **Status:** Open.

- **Affected code:** `src/network/CrossPointWebServer.cpp`, `CrossPointWebServer::handleUpload()` for multipart HTTP uploads, and `CrossPointWebServer::onWebSocketEvent()` / `abortWsUpload()` for WebSocket uploads.
- **Trigger / reproduction:** Through the built-in web file-transfer server, upload over an existing file and then abort the client, disconnect mid-transfer, fill the SD card, or inject a short write after the upload has started.
- **Observed / logically demonstrated failure:** Both upload transports build the final destination pathname and immediately call `Storage.remove(filePath)` when it already exists. They then stream new bytes directly into that canonical path. On abort or short write the WebSocket path deletes the partial file; multipart upload reports failure after the old file was already removed and can leave only a partial replacement. These handlers never retain a rollback copy of the original.
- **Likely root cause:** The web upload implementation uses delete-then-stream overwrite semantics rather than a staged replacement transaction.
- **Impact:** A failed browser or WebSocket upload can destroy a previously valid book or other user file even though the replacement never completed successfully.
- **Repair direction:** Stream uploads into a unique temporary file, require complete byte-count/write/close validation, and publish the new file transactionally only after success. Preserve the old destination until publication commits and restore it if replacement fails. Reuse a shared safe-replace helper where practical, and add aborted, short-write, disk-full, and disconnect regression tests for both upload transports.

- **Consolidation sources:** [automation/bug-scan-20260928-2327](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/599139f74e27c778eacc465b89a277091e7f1069/bugs.md); [automation/bug-scan-20260929-1325-final](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/a2a96bbe6fd61e31339ebf6cd83080c9375ff533/bugs.md); [automation/bug-scan-20260929-1325](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/339a210c68dcca5efda472896943769c30581343/bugs.md); [automation/bug-scan-20260930-0022](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/2b932092e31b6d6b85bd45c7ada99cb728994e26/bugs.md); [Drive: 2026-09-30 0022 MDT - automation-bug-scan-20260930-0022 - Diff](https://docs.google.com/spreadsheets/d/1VGiMSPAY900zqoow0Q_kZG_D-96pFkos_O65zZAw_mo/edit?usp=drivesdk); [Drive: 2026-09-29 1325 automation-bug-scan-20260929-1325-final Instructions](https://docs.google.com/spreadsheets/d/1YbJE6YPRZsBGC54dWnNamiLrLVwPHwvdp9kURLmtrQg/edit?usp=drivesdk); [Drive: 2026-09-29 1325 automation-bug-scan-20260929-1325-final Diff](https://docs.google.com/spreadsheets/d/12U7ZUH2XA16WgTIMC70fphq9fk2KesQeSGNgcP3NVE0/edit?usp=drivesdk); [Drive: 2026-09-30 0022 MDT - automation-bug-scan-20260930-0022 - Instructions](https://docs.google.com/spreadsheets/d/1hniZfzD9J_jLioHAEqaS74cAsknU2Ri9qm5BXSs-J9Y/edit?usp=drivesdk)

### 162. Web Settings silently omits the Font Family control when its JSON exceeds the fixed buffer

- **Status:** Open.

- **Affected code:** `src/network/CrossPointWebServer.cpp::handleGetSettings()`; `src/SettingsList.h::buildFontFamilySetting()`; `lib/EpdFont/SdCardFontRegistry.h::MAX_SD_FAMILIES`.
- **Trigger / reproduction:** Install enough SD font families that the JSON object for the dynamic `fontFamily` enum exceeds the fixed 512-byte per-setting buffer, then open the web Settings page or request its settings API. The registry supports up to 128 SD families, and the API includes every family name plus the two built-ins in one `options` array.
- **Observed / logically demonstrated failure:** `handleGetSettings()` serializes each complete setting into `char output[512]`; when `serializeJson()` reaches/exceeds that capacity, the code logs `Skipping oversized setting JSON` and continues without emitting that setting. The dynamic Font Family entry grows with the installed registry and can therefore disappear entirely from the response even though the fonts remain installed.
- **Likely root cause:** A variable-size enum is forced through a fixed per-setting serialization buffer, and overflow is handled by dropping the setting rather than streaming or growing the representation.
- **Impact:** As the font library grows, the web settings UI loses the Font Family control and cannot display or change the active family. This is distinct from bug #60, which concerns the on-device Font Family selector's own choice limit.
- **Repair direction:** Stream the setting JSON directly or use bounded PSRAM-backed/growable serialization sized from `measureJson()`; never silently omit a valid setting because its options are numerous. Add a regression with enough short and long family names to cross 512 bytes and verify the response remains valid JSON containing the complete `fontFamily` setting.

- **Consolidation sources:** [automation/bug-scan-20260928-2327](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/599139f74e27c778eacc465b89a277091e7f1069/bugs.md)

### 163. USB mass-storage detach during an open write leaves a zero-byte file and orphaned FAT clusters

- **Status:** Open.

- **Affected code:** `Drivers/usb_mass_storage/driver.c`: `volume_file_open_write()`, `volume_file_write()`, `volume_refresh()`, and `detach_volume()`.
- **Trigger / reproduction:** Mount a writable FAT16/FAT32 USB drive, open a new destination through the storage-volume API, write enough data to allocate at least one cluster, but before `volume_file_close(..., true)` succeeds force `host->poll()` or device enumeration to fail (or disconnect/reconnect the device).
- **Observed / logically demonstrated failure:** `volume_file_open_write()` creates the visible directory entry immediately with cluster/size still zero. Data writes allocate FAT clusters, but the directory entry is not updated with `first_cluster` and `size` until a successful committed close. `volume_refresh()` calls `detach_volume()` on host/enumeration failure; `detach_volume()` simply clears `file_state.active` and forgets the in-progress write. After reconnect, the directory can therefore contain a zero-byte destination while the already allocated cluster chain has no directory reference. A retry of the same filename is then rejected because the destination already exists.
- **Likely root cause:** The write transaction has no recoverable/staged namespace state across volume detach: the namespace entry is published before commit, while all rollback information lives only in volatile `file_state`.
- **Impact:** A transient USB-host fault or cable interruption during a write can leave persistent filesystem garbage: a misleading zero-byte file plus leaked FAT space, and normal retry cannot recreate the intended file without manual cleanup.
- **Repair direction:** Make new-file publication transactional. Prefer a temporary/hidden entry whose cluster chain is finalized and then published at commit; otherwise persist enough recovery state to delete the incomplete entry/free its chain on reattach. Do not discard an active writable `file_state` without either successful rollback or a defined recovery marker. Add an injected host-poll failure test after at least one cluster write and verify reconnect leaves neither the destination nor allocated orphan clusters.

- **Consolidation sources:** [automation/bug-scan-20260928-2354](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/adbef888b66176c0dd8e859e91629a93c1ceda23/bugs.md); [Drive: 2026-09-28 2354 automation-bug-scan-20260928-2354 diff](https://docs.google.com/document/d/1Q0TY6FxGuO5mz5-eim6LjlhC5BL6u24Ibi5DKNFyUUs/edit?usp=drivesdk); [Drive: 2026-09-28 2354 automation-bug-scan-20260928-2354 instructions](https://docs.google.com/document/d/1tPJ5PE9mW3TkqebBAGNIijAyJCz0rCccHTRJ2e4Tr50/edit?usp=drivesdk)

### 164. Release-manifest validation ignores its required-version flag and accepts unversioned app releases

- **Status:** Open.
- **Affected code:** `src/native/AppManifest.cpp::parseAppManifest()` and its `requireAppVersion` contract in `src/native/AppManifest.h`; callers include `src/native/NativeAppHost.cpp::loadAggregateCatalog()`, `loadCatalogManifests()`, `appCatalogDownloadWithProgress()`, and required-app installation.
- **Trigger / reproduction:** Pass an otherwise valid application manifest that omits the `version` field to `parseAppManifest(..., appVersion, true)`. The fallback/aggregate release-catalog paths explicitly make this call with `requireAppVersion=true`.
- **Observed / logically demonstrated failure:** The parser accepts a missing version whenever `versionNode.isNull()`, then explicitly discards the caller's requirement with `(void)requireAppVersion`. It therefore returns success even though the caller asked for a versioned release manifest. In fallback/aggregate catalog construction this can admit an unversioned release record with an empty parsed version and defer failure or inconsistent version handling to later update/install logic instead of rejecting the bad release metadata at its validation boundary.
- **Likely root cause:** Runtime compatibility for old installed sidecars was implemented by making version optional globally rather than honoring the existing flag that distinguishes legacy sidecars from newly published release manifests.
- **Impact:** The release validator does not enforce its advertised schema, so malformed/unversioned release metadata can be exposed as a valid catalog entry and can reach code that assumes a semantic version exists for update ordering and package publication.
- **Repair direction:** When `requireAppVersion` is true, reject a null/missing `version` before returning success; preserve legacy acceptance only when the flag is false. Add parser tests proving `requireAppVersion=false` accepts a legacy missing-version sidecar while `true` rejects it, plus catalog tests that an unversioned release never becomes visible/installable.

- **Consolidation sources:** [automation/bug-scan-20260929-0027](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1eb74f2f53befe4d3d3f366aab29e9c532ec5c1d/bugs.md); [Drive: 2026-09-29 0027 automation_bug-scan-20260929-0027 Instructions](https://docs.google.com/spreadsheets/d/1G5bshzTr7OOfLnKoqSLlCpfSwRYnS4ArEihTuix04hU/edit?usp=drivesdk); [Drive: 2026-09-29 0027 automation_bug-scan-20260929-0027 Diff](https://docs.google.com/spreadsheets/d/1QYZyfloUsYxmVpadMen1yitn8eelChTNer8hEGVmvfU/edit?usp=drivesdk)

### 165. Oversized PNG dimensions can overflow the EPUB framebuffer decoder's safety arithmetic

- **Status:** Open.
- **Affected code:** `lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp::validateImageDimensions()`; `lib/Epub/Epub/converters/PngToFramebufferConverter.cpp::requiredPngInternalBufferBytes()` and `decodeToFramebuffer()`.
- **Trigger / reproduction:** Open an EPUB containing a crafted PNG whose IHDR advertises very large positive dimensions that overflow 32-bit signed arithmetic. A deterministic arithmetic case is width `1073741825` and height `4`: both values are legal positive 31-bit PNG dimensions, but the product and the PNG row-buffer calculation exceed `INT_MAX`. The dimension-query path also narrows dimensions to `int16_t`, so this width appears as `1` to layout while the decoder later sees the original width.
- **Observed / logically demonstrated failure:** `validateImageDimensions()` evaluates `width * height` as signed `int` before comparing it with `MAX_SOURCE_PIXELS`; that multiplication overflows instead of reliably rejecting the image. The PNG-specific buffer guard independently performs `srcWidth * bytesPerPixel` and `(pitch + 1) * 2` as signed `int`, so the same oversized width can overflow `requiredInternal` to a small/negative value and bypass the `PNG_MAX_BUFFERED_PIXELS` check that exists specifically to prevent PNGdec from overrunning its internal scanline storage.
- **Likely root cause:** Size validation is performed with overflow-prone signed products and no explicit upper bound on each source dimension before arithmetic.
- **Impact:** A malformed or hostile embedded PNG can defeat the decoder's memory-safety guard and reach PNGdec with source geometry far beyond the configured scanline capacity, risking decode failure, memory corruption, or a device crash while rendering a book.
- **Repair direction:** Reject non-positive dimensions and impose explicit per-axis maxima before any multiplication. Perform products and row-byte calculations in checked `uint64_t`/`size_t` arithmetic, reject overflow before narrowing, and validate dimension queries before storing them in `ImageDimensions`. Add regression PNG headers around `INT_MAX`, the pixel-count boundary, and widths that overflow the internal-row calculation.

- **Consolidation sources:** [automation/bug-scan-20260929-0027](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1eb74f2f53befe4d3d3f366aab29e9c532ec5c1d/bugs.md); [automation/bug-scan-20260929-1521](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/3b5ee347c5f261f42ffb47c4e42fa8d6938d0b1c/bugs.md); [Drive: 2026-09-29 1521 MDT - automation-bug-scan-20260929-1521 - Diff](https://docs.google.com/spreadsheets/d/1cTBgkQqcH5XClkBpjrNyYB1r77lq226fvwpP8SzDBVo/edit?usp=drivesdk); [Drive: 2026-09-29 1521 MDT - automation-bug-scan-20260929-1521 - Diff](https://docs.google.com/document/d/1n7GGQT0HAu48H5Kslu1if8a5qKqt15oE2-9WHM0l_sA/edit?usp=drivesdk); [Drive: 2026-09-29 0027 automation_bug-scan-20260929-0027 Instructions](https://docs.google.com/spreadsheets/d/1G5bshzTr7OOfLnKoqSLlCpfSwRYnS4ArEihTuix04hU/edit?usp=drivesdk); [Drive: 2026-09-29 0027 automation_bug-scan-20260929-0027 Diff](https://docs.google.com/spreadsheets/d/1QYZyfloUsYxmVpadMen1yitn8eelChTNer8hEGVmvfU/edit?usp=drivesdk); [Drive: 2026-09-29 1521 MDT - automation-bug-scan-20260929-1521 - Instructions](https://docs.google.com/spreadsheets/d/1Ip2qZ7MWIuO0ktAYH7PbAvU1eU84E53kkBOAr1iYdcs/edit?usp=drivesdk)

### 166. Clear Reading Cache reports success after cache-directory open or enumeration I/O failures

- **Status:** Open.

- **Affected code:** `src/native/NativeCacheBridge.cpp::clearReadingCache()`; result presentation in `Apps/clear_cache.c::render_result()`.
- **Trigger / reproduction:** With multiple `/.crosspoint/epub_*` or `xtc_*` cache directories present, fault the SD directory iterator after one matching directory has been returned and removed, so the next `root.openNextFile()` returns an invalid handle before true end-of-directory. A simpler trigger is to make opening an existing `/.crosspoint` directory fail transiently.
- **Observed / logically demonstrated failure:** An invalid result from `openNextFile()` is used as the `for` loop terminator with no error check, so an I/O failure is indistinguishable from normal EOF. The function then closes the root and returns `true` with `failed_count == 0`; the app renders **Cache cleared** even though only a prefix of caches was visited. At initial open, `!root || !root.isDirectory()` is also treated as a successful “directory unavailable” result, so a transient open/read failure on an existing cache directory is shown as **Nothing to clear** rather than an error.
- **Likely root cause:** Directory absence/EOF and storage I/O failure are collapsed into the same invalid-handle states, while `clearReadingCache()` treats those states as successful completion.
- **Impact:** Cache cleanup can be only partially performed while the UI claims success, leaving stale or corrupt generated reading data behind. An SD fault can also be falsely presented as an empty cache.
- **Repair direction:** Distinguish confirmed directory absence from open failure (for example, check existence first and treat an existing-but-unopenable directory as failure), and use an enumeration path that exposes I/O/error status separately from EOF. If enumeration fails after partial deletion, return failure or increment `failed_count` and report partial completion. Add fault-injection tests for initial open failure and mid-directory enumeration failure.

- **Consolidation sources:** [automation/bug-scan-20260929-0027](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1eb74f2f53befe4d3d3f366aab29e9c532ec5c1d/bugs.md); [automation/bug-scan-20260929-1421](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/e629428d0573b0b6e3bbf952e77f16495bc60fa7/bugs.md); [automation/bug-scan-20260930-0419](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/8f3a068b9f2d6105e008255713ee5086691a134a/bugs.md); [Drive: 2026-09-30 0419 MDT - automation_bug-scan-20260930-0419 - Diff](https://docs.google.com/spreadsheets/d/1o6Bfq-I4ygMn_Ai6dZFXLxodBuN4p08HsGPgkuWaXGo/edit?usp=drivesdk); [Drive: 2026-09-29 1421 MDT - automation-bug-scan-20260929-1421 - Instructions](https://docs.google.com/spreadsheets/d/1PxZ2lsUdx2DcGC4s3qaF4cBQ8agSIvjTCrsJu4LDEws/edit?usp=drivesdk); [Drive: 2026-09-29 1421 MDT - automation-bug-scan-20260929-1421 - Diff](https://docs.google.com/spreadsheets/d/1xPKDnEDkwHqADDFeUzABbYFT6iT-RFeG66EhMkpBByE/edit?usp=drivesdk); [Drive: 2026-09-29 0027 automation_bug-scan-20260929-0027 Instructions](https://docs.google.com/spreadsheets/d/1G5bshzTr7OOfLnKoqSLlCpfSwRYnS4ArEihTuix04hU/edit?usp=drivesdk); [Drive: 2026-09-29 0027 automation_bug-scan-20260929-0027 Diff](https://docs.google.com/spreadsheets/d/1QYZyfloUsYxmVpadMen1yitn8eelChTNer8hEGVmvfU/edit?usp=drivesdk); [Drive: 2026-09-30 0419 MDT - automation_bug-scan-20260930-0419 - Instructions](https://docs.google.com/spreadsheets/d/1PyHqC8xFRzQs02Mt1-ory7jR4LOiSeqVqJ1dFvpgQzg/edit?usp=drivesdk)

### 167. ZIP trailing-NUL size arithmetic wraps for a maximum-size entry

- **Status:** Open.
- **Affected code:** `lib/ZipFile/ZipFile.cpp::readFileToMemory()`; callers include `lib/Epub/Epub.cpp::readItemContentsToBytes()`, which requests a trailing NUL for XML/HTML text.
- **Trigger / reproduction:** Use ZIP metadata declaring an uncompressed entry size of `0xFFFFFFFF`, then read that entry with `trailingNullByte=true`.
- **Observed / logically demonstrated failure:** `inflatedDataSize` is a `uint32_t`, so `inflatedDataSize + 1` wraps to zero before it is assigned to `dataSize`. The routine can therefore allocate zero bytes and then proceed using the original `0xFFFFFFFF` declared length for the stored read or inflate destination, followed by a terminator write at that declared index, instead of rejecting the impossible in-memory request.
- **Likely root cause:** The extra-byte size calculation is performed in the archive field's 32-bit type without checked conversion to `size_t` or an overflow guard.
- **Impact:** Corrupt or extreme ZIP/EPUB size metadata can turn an in-memory text read into invalid memory access rather than a clean size/allocation failure, risking heap damage or a reset.
- **Repair direction:** Enforce a practical per-entry in-memory limit, check the declared size before converting it, and check addition before reserving the trailing byte. Add boundary tests for the configured maximum and `UINT32_MAX` with and without `trailingNullByte`.

- **Consolidation sources:** [automation/bug-scan-20260929-0127-findings](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b126c4282b721ea72cd5972a2779992bfaf6441c/bugs.md); [Drive: 2026-09-29 0127 MDT - automation-bug-scan-20260929-0127-findings - Instructions](https://docs.google.com/spreadsheets/d/1ad4bmYdzefozwBelKNdUD846o6B6SZ6_W2j2E23XvHw/edit?usp=drivesdk); [Drive: 2026-09-29 0127 MDT - automation-bug-scan-20260929-0127-findings - Diff](https://docs.google.com/spreadsheets/d/1Bl2Lj6n40DNLK8lCt8zLIjps7eeAa74hQyQ376hxUh8/edit?usp=drivesdk)

### 168. EPUB BMP conversion can report success after destination writes fail and retain a truncated cover cache

- **Status:** Open.
- **Affected code:** `lib/PngToBmpConverter/PngToBmpConverter.cpp` and `lib/JpegToBmpConverter/JpegToBmpConverter.cpp`, including BMP header/row writers; consumers `lib/Epub/Epub.cpp::generateCoverBmp()` and `generateThumbBmp()`.
- **Trigger / reproduction:** Generate an EPUB JPG/PNG cover or thumbnail while forcing the destination `FsFile` to return a short or zero write after opening successfully, such as an SD write fault or full-volume condition.
- **Observed / logically demonstrated failure:** Both converters call `Print::write()` for BMP headers, palettes, and pixel rows but never check the returned byte count and keep no output-error state. If source decoding succeeds, the converter returns `true` even when the BMP stream is incomplete. `Epub::generateCoverBmp()` and `generateThumbBmp()` trust that boolean, retain the partial file, and future calls return success immediately from `Storage.exists(...)` without validating the cached BMP.
- **Likely root cause:** Conversion success reflects source decode completion only; destination I/O success is not part of the converter result, while the caller treats file existence as proof of a valid cache.
- **Impact:** A transient SD write fault can leave a corrupt cover or thumbnail that is treated as successfully generated and is not automatically regenerated, producing missing or broken artwork until the cache is cleared.
- **Repair direction:** Check every BMP output write for the exact expected byte count, propagate output failure through the converter, and return false on any short write. Generate into a staged file, require successful close plus basic BMP validation, then publish the final cache. Add fault-injection tests for header and pixel-row write failures.

- **Consolidation sources:** [automation/bug-scan-20260929-0127-findings](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b126c4282b721ea72cd5972a2779992bfaf6441c/bugs.md); [automation/bug-scan-20260929-1227](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/6e24332a2bf14ad272bb4b8d7944e5e9a9cd1e1c/bugs.md); [Drive: 2026-09-29 1227 MDT - automation-bug-scan-20260929-1227 - Instructions](https://docs.google.com/document/d/1W1g8tZYLjNvZt4NIm61ESqN6j60PCManQaqLmS_7yj0/edit?usp=drivesdk); [Drive: 2026-09-29 1227 MDT - automation-bug-scan-20260929-1227 - Diff](https://docs.google.com/document/d/1fZ8vZ0C1ZjmKnHULiYYQSDc1Cg7AqNSix4w-kpxSVwY/edit?usp=drivesdk); [Drive: 2026-09-29 0127 MDT - automation-bug-scan-20260929-0127-findings - Instructions](https://docs.google.com/spreadsheets/d/1ad4bmYdzefozwBelKNdUD846o6B6SZ6_W2j2E23XvHw/edit?usp=drivesdk); [Drive: 2026-09-29 0127 MDT - automation-bug-scan-20260929-0127-findings - Diff](https://docs.google.com/spreadsheets/d/1Bl2Lj6n40DNLK8lCt8zLIjps7eeAa74hQyQ376hxUh8/edit?usp=drivesdk)

### 169. Time Zone can commit a city or exit two navigation levels from one held button press

- **Status:** Open.

- **Affected code:** `Apps/time_zone.c`, `app_main()` and `activate()`; input semantics come from `src/native/NativeAppHost.cpp::pollInput()`.
- **Trigger / reproduction:** Open **Time Zone**, highlight a region different from the currently configured one, then press and hold Confirm long enough to span two 50 ms app polls. A second reproduction is to enter a city's list and hold Back across two polls.
- **Observed / logically demonstrated failure:** `NativeAppHost::pollInput()` publishes the current button level on every call through `MappedInputManager::isPressed()`. Time Zone has no edge/release gate. On the first held-Confirm poll, `activate()` switches from region mode to city mode, initializes the city selection (normally index 0 when entering a different region), renders, and returns. The next poll sees the same still-held Confirm bit and immediately calls `zones->select_city()`, committing that city and exiting the app. Likewise, a held Back first leaves city mode and then, on the next poll, exits the app from region mode. One physical press can therefore execute two distinct navigation actions.
- **Likely root cause:** The app treats the level-triggered native-app button mask as one-shot UI events even though transitions between screens can occur while the same physical button remains asserted.
- **Impact:** Merely holding Confirm while opening a region can unintentionally change the system timezone to the first city in that region, and holding Back can skip the intermediate region screen. This is separate from the existing Time Zone persistence-failure and 96-row truncation reports.
- **Repair direction:** Add rising-edge/release rearming around button actions, matching the pattern already used by `Apps/settings.c`, or consume the edge-based UI event API instead of raw level bits. Reset/rearm deliberately across region/city transitions. Add regressions that keep Confirm and Back asserted for several polls and verify exactly one navigation/action occurs per physical press.

- **Consolidation sources:** [automation/bug-scan-20260929-0223](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/aafc7d5defd59b404ce8bbd45ad3e1581b56d805/bugs.md); [automation/bug-scan-20260929-1421](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/e629428d0573b0b6e3bbf952e77f16495bc60fa7/bugs.md); [Drive: 2026-09-29 1421 MDT - automation-bug-scan-20260929-1421 - Instructions](https://docs.google.com/spreadsheets/d/1PxZ2lsUdx2DcGC4s3qaF4cBQ8agSIvjTCrsJu4LDEws/edit?usp=drivesdk); [Drive: 2026-09-29 1421 MDT - automation-bug-scan-20260929-1421 - Diff](https://docs.google.com/spreadsheets/d/1xPKDnEDkwHqADDFeUzABbYFT6iT-RFeG66EhMkpBByE/edit?usp=drivesdk); [Drive: 2026-09-29 0223 MDT - automation-bug-scan-20260929-0223 - Instructions](https://docs.google.com/document/d/1ZfhQ2F_rfoibch3SfcN1XB35w_uSd6-FOmSZ0S8War4/edit?usp=drivesdk); [Drive: 2026-09-29 0223 MDT - automation-bug-scan-20260929-0223 - Diff](https://docs.google.com/document/d/1Ta7Gf8gV44oQ1S9273fWJRyZ7odf_TIJ7ddOEzSeP0U/edit?usp=drivesdk)

### 170. Ask can restore a conversation after exit when session-file deletion fails

- **Status:** Open.
- **Affected code:** `Apps/llm_ask.c`, `clear_session_file()`, Back/Exit handling in `app_main()`, and `load_session()`; deletion status comes from `src/native/NativePlatformBridge.cpp::removeFile()`.
- **Trigger / reproduction:** Create an Ask conversation so `/.crosspoint/llm_ask.session` exists, force `remove_file` to fail while leaving Ask, then restore storage and relaunch Ask.
- **Observed / logically demonstrated failure:** `clear_session_file()` discards the boolean result from `storage->remove_file(SESSION_PATH)`. Back and Exit then leave the app as though cleanup succeeded. The retained session still has valid magic/version, so the next `load_session()` accepts it and displays the previous conversation again.
- **Likely root cause:** Durable session cleanup is treated as fire-and-forget even though the storage API reports deletion failure.
- **Impact:** Conversation text intended to be discarded on exit can persist and unexpectedly reappear on a later launch.
- **Repair direction:** Handle deletion failure explicitly and make persisted-session invalidation transactional. A failed remove must not leave a session that the next launch accepts as current. Add a fault-injection test for exit-time remove failure and relaunch.

- **Consolidation sources:** [automation/bug-scan-20260929-0223](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/aafc7d5defd59b404ce8bbd45ad3e1581b56d805/bugs.md); [Drive: 2026-09-29 0223 MDT - automation-bug-scan-20260929-0223 - Instructions](https://docs.google.com/document/d/1ZfhQ2F_rfoibch3SfcN1XB35w_uSd6-FOmSZ0S8War4/edit?usp=drivesdk); [Drive: 2026-09-29 0223 MDT - automation-bug-scan-20260929-0223 - Diff](https://docs.google.com/document/d/1Ta7Gf8gV44oQ1S9273fWJRyZ7odf_TIJ7ddOEzSeP0U/edit?usp=drivesdk)

### 171. ST-LINK disconnect or close-time command failure permanently retains the USB claim/session

- **Status:** Open.
- **Affected code:** `Drivers/usb_stlink/driver.c`, `close_probe()`, `poll_probes()`, session bookkeeping, and `quiesce()`.
- **Trigger / reproduction:** Open an ST-LINK SWD/SWIM session, unplug the probe or otherwise make `current_mode()` or `exit_mode()` fail, then close the session and attempt to quiesce/reload the provider or reconnect and reopen the probe.
- **Observed / logically demonstrated failure:** `close_probe()` returns immediately when `current_mode()` or `exit_mode()` fails, before `host->host.release(..., s->claim)` and before clearing the `session_slot`. `poll_probes()` clears vanished probe/inspection state but not matching sessions. After disconnect, the protocol close cannot recover, the token remains nonzero indefinitely, and `quiesce()` keeps returning false.
- **Likely root cause:** Graceful protocol-exit success is incorrectly required before releasing local USB-host ownership and retiring software session state.
- **Impact:** A cable pull or transient ST-LINK failure can wedge `debug.vendor.stlink`, prevent clean driver unload/update, and block later sessions until restart.
- **Repair direction:** Make protocol mode-exit best-effort during close, always attempt host-claim release and retire the local session slot, and also retire sessions whose device disappears during discovery. Preserve the protocol error separately. Add detach and injected close-failure tests proving `quiesce()` becomes true and a later session can open.

- **Consolidation sources:** [automation/bug-scan-20260929-0223](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/aafc7d5defd59b404ce8bbd45ad3e1581b56d805/bugs.md); [Drive: 2026-09-29 0223 MDT - automation-bug-scan-20260929-0223 - Instructions](https://docs.google.com/document/d/1ZfhQ2F_rfoibch3SfcN1XB35w_uSd6-FOmSZ0S8War4/edit?usp=drivesdk); [Drive: 2026-09-29 0223 MDT - automation-bug-scan-20260929-0223 - Diff](https://docs.google.com/document/d/1Ta7Gf8gV44oQ1S9273fWJRyZ7odf_TIJ7ddOEzSeP0U/edit?usp=drivesdk)

### 172. File Browser can rename the wrong item when its handoff session fails to save

- **Status:** Open.

- **Affected code:** `Apps/file_browser.c`, especially `save_session()`, `request_rename()`, startup `load_session()`, and `consume_handoff_results()`.
- **Trigger / reproduction:** Open File Browser in a non-root SD folder, select an item, choose **Rename**, and force `storage->write_file_atomic(SESSION_PATH, ...)` to fail while allowing `system_ui->keyboard_request(..., RENAME_COOKIE)` to succeed. Enter a different valid name in the keyboard and return to File Browser. Keep an unrelated item at the root so the resumed app has a selectable row there.
- **Observed / logically demonstrated failure:** `save_session()` ignores the boolean result of `write_file_atomic()` and `request_rename()` proceeds with the keyboard handoff anyway. The app then exits. On restart it initializes `base_path` to `"/"`, loads that directory, and only afterwards attempts `load_session()`. With the new session absent (or with an older stale session still on disk), the rename continuation calls `selected_name()` against the wrong directory/selection. It then constructs `source_vfs` from that unrelated `old_name` and current `base_path` and can rename that item using the name intended for the original selection.
- **Likely root cause:** The handoff depends on durable continuation state, but the session write is treated as best-effort and the returned keyboard result contains only a cookie, not the canonical source path needed to identify the original item.
- **Impact:** A transient SD/settings write failure immediately before a rename can rename an unrelated file or directory after the keyboard returns. This is a data-integrity failure, not merely a lost UI selection.
- **Repair direction:** Make `save_session()` return success and do not issue the keyboard handoff unless the continuation state is durably written. Persist the operation and canonical source path, then validate that exact source on resume before mutating it; never derive the rename source from whatever row happens to be selected after restart. Treat a stale/missing session as a cancelled rename. Add a fault-injection test for session-write failure from a nested folder and prove no root or stale-session item is renamed.

- **Consolidation sources:** [automation/bug-scan-20260929-0321](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1119b86c20c4a8024ef0f0b1e3ed416ce0b0e8ea/bugs.md); [Drive: 2026-09-29 0321 MDT - automation-bug-scan-20260929-0321 - Instructions](https://docs.google.com/spreadsheets/d/1olIEa0GA1zbdPFGAPQbYGSKvj7INIdabz3rmzWNM4x0/edit?usp=drivesdk); [Drive: 2026-09-29 0321 MDT - automation-bug-scan-20260929-0321 - Diff](https://docs.google.com/spreadsheets/d/1UP3gJ6ntPpZsnttw6gXKoPoWZ5guN8F2pWhAClKpZxA/edit?usp=drivesdk)

### 173. Failed EPUB 3 nav extraction is reported as success and suppresses a valid NCX fallback

- **Status:** Open.

- **Affected code:** `lib/Epub/Epub.cpp`, `Epub::parseTocNavFile()`, `Epub::parseTocNcxFile()`, and the TOC selection logic in `Epub::load()`.
- **Trigger / reproduction:** Use an EPUB whose OPF declares an EPUB 3 nav item that cannot be extracted from the ZIP (for example the declared nav entry is missing/corrupt) while also providing a valid compatibility NCX item.
- **Observed / logically demonstrated failure:** Both TOC parsers call `readItemContentsToStream(...)` to create their temporary file but discard its boolean result. If the nav extraction fails before writing data, the temporary file can be empty. `parseTocNavFile()` then opens that zero-byte file, the parse loop executes zero times, and the function unconditionally logs success and returns `true`. In `Epub::load()`, that sets `tocParsed = true`, so the existing `if (!tocParsed && !tocNcxItem.empty())` fallback never tries the valid NCX. `parseTocNcxFile()` has the same false-success path for its own extraction failure.
- **Likely root cause:** The ZIP-to-temporary-file copy is treated as fire-and-forget, and parser success is inferred from “no loop iteration reported an error” rather than from a successful extraction and completed parse.
- **Impact:** A book can lose its table of contents even though it contains a valid fallback TOC; corrupt/missing TOC resources are also misreported as successfully parsed and can be cached as an empty TOC.
- **Repair direction:** Check `readItemContentsToStream()` before reopening either temporary TOC file; on failure close/remove the temp file and return `false`. Reject zero-byte/incomplete parses and make parser completion explicit so `Epub::load()` reliably falls back from nav to NCX. Add regressions for missing nav + valid NCX, corrupt nav + valid NCX, and failed NCX extraction.

- **Consolidation sources:** [automation/bug-scan-20260929-0321](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/1119b86c20c4a8024ef0f0b1e3ed416ce0b0e8ea/bugs.md); [Drive: 2026-09-29 0321 MDT - automation-bug-scan-20260929-0321 - Instructions](https://docs.google.com/spreadsheets/d/1olIEa0GA1zbdPFGAPQbYGSKvj7INIdabz3rmzWNM4x0/edit?usp=drivesdk); [Drive: 2026-09-29 0321 MDT - automation-bug-scan-20260929-0321 - Diff](https://docs.google.com/spreadsheets/d/1UP3gJ6ntPpZsnttw6gXKoPoWZ5guN8F2pWhAClKpZxA/edit?usp=drivesdk)

### 174. A font family with no files is accepted and reported as installed successfully

- **Status:** Open.

- **Affected code:** `src/native/NativeFontBridge.cpp::refreshCatalog()` and `installFamily()`; `src/FontInstaller.cpp::ensureFamilyDir()`; presentation in `Apps/font_manager.c`.
- **Trigger / reproduction:** Serve a version-valid font manifest containing a family with a valid family name but an absent or empty `files` array, refresh Manage Fonts, and select that family for installation.
- **Observed / logically demonstrated failure:** `refreshCatalog()` never requires a family to contain at least one file, so the empty family is accepted. `installFamily()` creates the family directory, executes a zero-iteration file loop, refreshes the registry, then unconditionally sets `family.installed = true` and returns `T5_FONT_OK`. Font Manager reports success/Installed even though no `.cpfont` file was installed and the registry has no usable family content to load.
- **Likely root cause:** Manifest validation checks each file only if one exists, and installation success is inferred from loop completion rather than a non-empty validated package plus post-install registry recognition.
- **Impact:** A malformed catalog can produce a phantom successful installation, leave empty font directories on SD, and present an unusable family as installed until later state is refreshed.
- **Repair direction:** Require `files` to be a present non-empty array during catalog validation, reject zero-file families before they enter live state, and verify the installed family is actually discoverable after registry refresh before reporting success. Remove any staged empty directory on failure and add empty/missing-files regression cases.

- **Consolidation sources:** [automation/bug-scan-20260929-0526](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/ac10238dae4c53d985ef85309e535a23d7a98ff2/bugs.md); [Drive: 2026-09-29 0526 MDT - automation-bug-scan-20260929-0526 - Instructions](https://docs.google.com/document/d/1NDxyuhCjLL4X1lD0EejpL9Li6MgzBx8jZGDDc9BCkdk/edit?usp=drivesdk); [Drive: 2026-09-29 0526 MDT - automation-bug-scan-20260929-0526 - Diff](https://docs.google.com/document/d/1crJqKG5skmKN8kGFAIAvJW_JYqZ2rpuPX2hec556r4U/edit?usp=drivesdk)

### 175. ZIP EOCD discovery can mistake an EOCD byte sequence inside a valid ZIP comment for the real record

- **Status:** Open.

- **Affected code:** `lib/ZipFile/ZipFile.cpp::ZipFile::loadZipDetails()`; downstream central-directory users such as `loadFileStatSlim()`, `findFirstBySuffix()`, and EPUB/archive consumers.
- **Trigger / reproduction:** Create an otherwise valid ZIP whose comment is within the currently scanned final 1 KiB and contains the four literal EOCD signature bytes at a position with at least 22 bytes remaining before EOF. Open an entry through `ZipFile`.
- **Observed / logically demonstrated failure:** `loadZipDetails()` scans backward for the first raw `0x06054b50` signature and immediately accepts it. It does not verify that the candidate's EOCD comment-length field makes the record end exactly at EOF, nor validate the central-directory range before setting `zipDetails.isSet`. Because ZIP comments are arbitrary bytes, an embedded EOCD-looking sequence later than the real EOCD is selected and comment bytes are interpreted as `totalEntries` and `centralDirOffset`. Subsequent entry lookup seeks to bogus offsets or reports a valid archive unreadable.
- **Likely root cause:** EOCD discovery recognizes a byte pattern rather than validating the complete EOCD candidate structure and continuing the backward search when a candidate is invalid.
- **Impact:** Standards-valid ZIP, EPUB, or ROM archives with particular legal comments fail deterministically or expose bogus archive metadata. This is distinct from the known greater-than-1-KiB comment scan-range limitation.
- **Repair direction:** For each signature candidate, decode the full EOCD safely and require `candidate_offset + 22 + comment_length == file_size`, supported single-disk fields, and a bounded/consistent central-directory range. Reject a false candidate and continue scanning backward. Also require the final-window read to return the requested byte count. Add a regression ZIP whose comment contains a fake EOCD signature after the real one.

- **Consolidation sources:** [automation/bug-scan-20260929-0623-final](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/a0eeba8b922c599a6b21700ab44037089cb37b92/bugs.md); [automation/bug-scan-20260929-0623-findings](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b1463ab6ef1fc1a6345ea660e56ae484b98b07b9/bugs.md); [Drive: 2026-09-29 0623 MDT - automation-bug-scan-20260929-0623-final - Instructions](https://docs.google.com/spreadsheets/d/1_in6hCljLxqiyI5MrjIzkrraPXJuEllimsyAhbStfjQ/edit?usp=drivesdk); [Drive: 2026-09-29 0623 MDT - automation-bug-scan-20260929-0623-findings - Instructions](https://docs.google.com/document/d/1MFdtmLQczvKYtfqT4o_4BxJt-W0lax5qxpBBYoeyBcQ/edit?usp=drivesdk); [Drive: 2026-09-29 0623 MDT - automation-bug-scan-20260929-0623-findings - Diff](https://docs.google.com/document/d/1tx4_PKHXrJvCuZyT8yr6fDOBHKQ1W_l_5VsZu7TB7xw/edit?usp=drivesdk)

### 176. GPS auto-baud remains permanently locked to a stale baud after the receiver changes speed

- **Status:** Open.

- **Affected code:** `Drivers/gps_nmea/driver.c::parse()`, `read_state()`, and `begin_baud()`.
- **Trigger / reproduction:** Start the GPS provider with a receiver producing checksum-valid NMEA at either supported baud (9600 or 38400), allowing `parse()` to set `locked = true`. Without unloading the provider, reset or reconfigure the receiver so it resumes NMEA output at the other supported baud, then wait longer than the normal 1600 ms probe interval and continue polling GPS state.
- **Observed / logically demonstrated failure:** Once a checksum-valid sentence is seen, `parse()` sets `locked = true` and `fix.receiver_detected = 1`. The only baud-switch condition in `read_state()` is guarded by `!locked`; there is no silence/error timeout that clears the lock. After a baud change, fix freshness expires, but the provider listens at the old baud forever and continues reporting the receiver as detected instead of resuming 9600/38400 probing. Recovery requires stopping and restarting the provider.
- **Likely root cause:** Baud detection is modeled as a permanent boolean latch rather than a lease on recently observed valid NMEA traffic.
- **Impact:** A GNSS module reset or runtime baud reconfiguration can strand GPS in a permanent detected-but-no-fix state even though the receiver is actively sending at the other baud the driver already supports.
- **Repair direction:** Track the last checksum-valid supported NMEA sentence time for the selected baud. After a bounded silence or invalid-stream interval, clear the baud lock and receiver-detected state and resume alternating supported bauds. Add a test stream that acquires lock at one baud, goes silent, then resumes valid GGA/RMC at the other baud without restarting the driver.

- **Consolidation sources:** [automation/bug-scan-20260929-0623-final](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/a0eeba8b922c599a6b21700ab44037089cb37b92/bugs.md); [automation/bug-scan-20260929-0623-findings](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b1463ab6ef1fc1a6345ea660e56ae484b98b07b9/bugs.md); [Drive: 2026-09-29 0623 MDT - automation-bug-scan-20260929-0623-final - Instructions](https://docs.google.com/spreadsheets/d/1_in6hCljLxqiyI5MrjIzkrraPXJuEllimsyAhbStfjQ/edit?usp=drivesdk); [Drive: 2026-09-29 0623 MDT - automation-bug-scan-20260929-0623-findings - Instructions](https://docs.google.com/document/d/1MFdtmLQczvKYtfqT4o_4BxJt-W0lax5qxpBBYoeyBcQ/edit?usp=drivesdk); [Drive: 2026-09-29 0623 MDT - automation-bug-scan-20260929-0623-findings - Diff](https://docs.google.com/document/d/1tx4_PKHXrJvCuZyT8yr6fDOBHKQ1W_l_5VsZu7TB7xw/edit?usp=drivesdk)

### 177. Firmware-update fallback checks only the repository-wide latest release, so an app/driver release can hide the newest firmware

- **Status:** Open.

- **Affected code:** `src/network/OtaUpdater.cpp`, `latestReleaseUrl` and the legacy fallback in `OtaUpdater::checkForUpdate()`; `lib/JsonParser/ReleaseJsonParser.cpp`, firmware-asset selection.
- **Trigger / reproduction:** Make `release-index.json` unavailable or unreadable so `checkForUpdate()` enters its fallback path. Publish an app-only or driver-only GitHub release after the most recent firmware release, which is valid in this repository's independently versioned release model, then run Firmware Update.
- **Observed / logically demonstrated failure:** The fallback requests `/repos/michaelrolphone-cmyk/T5S3-Reader/releases/latest`, which returns one repository-wide release. `ReleaseJsonParser` then searches only that release for `firmware-<board>.bin`. If the newest release is an app/driver release, the parser reports no firmware asset and `checkForUpdate()` returns `NO_UPDATE`; it never searches older releases for the newest firmware tag. A valid newer firmware release can therefore exist and remain undiscoverable exactly when the release index is unavailable and the fallback is supposed to provide recovery.
- **Likely root cause:** The fallback predates independent app/driver/firmware releases and assumes the repository's generic latest release is necessarily a firmware release.
- **Impact:** Loss or corruption of the release index can make Firmware Update falsely report no firmware update merely because unrelated package activity is newer than the firmware release.
- **Repair direction:** Make fallback firmware-specific: query a firmware release/tag pointer, enumerate recent releases until the first valid `firmware-v*` release containing the board asset is found, or maintain a dedicated immutable/latest-firmware endpoint. Do not treat an unrelated latest release as evidence that no firmware update exists. Add a regression fixture where an app release is newest but a firmware release immediately precedes it.

- **Consolidation sources:** [automation/bug-scan-20260929-0725](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/7c3008d0876ab9bb0a728aca781b63fbfa9613f3/bugs.md); [Drive: 2026-09-29 0725 MDT - automation-bug-scan-20260929-0725 - instructions](https://docs.google.com/spreadsheets/d/1-4jDQiaUOBHv4-fTEqOZCIfyr8g9jYm1HMFC0fqAc_o/edit?usp=drivesdk); [Drive: 2026-09-29 0725 MDT - automation-bug-scan-20260929-0725 - diff](https://docs.google.com/spreadsheets/d/1AJPUdv9PVLhPLWMG1wuU6fzACHMVp59JWvFPEDsjuQ0/edit?usp=drivesdk)

### 178. BookMetadataCache treats short SD writes and close failures as successful cache publication

- **Status:** Open.

- **Affected code:** `lib/Epub/Epub/BookMetadataCache.cpp`, especially `writeSpineEntry()`, `writeTocEntry()`, `endContentOpfPass()`, `endTocPass()`, and `buildBookBin()`; `lib/Serialization/Serialization.h`, the `FsFile` `writePod()` / `writeString()` helpers; caller `lib/Epub/Epub.cpp::Epub::load()`.
- **Trigger / reproduction:** Force the SD card to short-write, become full, or fail a flush/close while an EPUB is being indexed and `spine.bin.tmp`, `toc.bin.tmp`, or final `book.bin` is being written.
- **Observed / logically demonstrated failure:** The `FsFile` serialization helpers return `void` and discard every `file.write()` byte count. The cache builders consequently cannot detect a short header/string/LUT/entry write. The two pass-ending functions call `close()` and unconditionally return `true`, while `buildBookBin()` likewise closes all three files, logs `Successfully built book.bin`, and returns `true` without checking write or close status. A failed cache write can therefore be acknowledged as complete and leave a truncated canonical `book.bin` or incomplete temporary input for the final build. The subsequent reload may fail late or, when a plausible prefix exists, accept malformed cache state.
- **Likely root cause:** The cache serialization layer has no error-propagating write contract, and cache publication writes directly to the canonical artifact without a verified stage.
- **Impact:** Transient SD exhaustion or media errors during indexing can leave an EPUB with a persistent corrupt cache while the indexing path reports success through the write phase. Later opens can fail, show wrong metadata/navigation, or repeatedly consume the bad cache instead of cleanly rebuilding.
- **Repair direction:** Make all `FsFile` serialization writes return success only on exact byte counts and propagate failures through spine/TOC creation and pass completion. Check flush/close results. Build the final cache into a new staged file, close and validate it completely, then publish it atomically; discard the stage on any error and preserve/rebuild from the last known-good cache. Add fault-injection tests for short writes in metadata, LUT, spine/TOC entries, and final close.

- **Consolidation sources:** [automation/bug-scan-20260929-0823](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/6a1c2dd84e3e922a799f9c29176cf2706bafc209/bugs.md); [Drive: 2026-09-29 0823 MDT - automation-bug-scan-20260929-0823 - Instructions](https://docs.google.com/spreadsheets/d/1mGhMQ7hk2vMFB7TIHJQ8JXV-Prl3VV31Pt4vDfr17GA/edit?usp=drivesdk); [Drive: 2026-09-29 0823 MDT - automation-bug-scan-20260929-0823 - Diff](https://docs.google.com/spreadsheets/d/1vJqjCYlDdK_38-EdVcug_G3EVGKhGAIet-6GhueGFhg/edit?usp=drivesdk)

### 179. XTC metadata parsing ignores the header metadata offset and reads title/author from fixed addresses

- **Status:** Open.

- **Affected code:** `lib/Xtc/Xtc/XtcTypes.h`, `XtcHeader::metadataOffset`; `lib/Xtc/Xtc/XtcParser.cpp`, `XtcParser::open()`, `readTitle()`, and `readAuthor()`; downstream `Xtc::getTitle()/getAuthor()`, Recent Books, Home, and screenshot metadata.
- **Trigger / reproduction:** Create an otherwise accepted XTC/XTCH with `hasMetadata = 1` and place its metadata block at a valid non-default location identified by `metadataOffset` (for example `0x100`), with a known title and author there. Keep bytes at `0x38` and `0xB8` different, then open the book.
- **Observed / logically demonstrated failure:** The parsed header has `metadataOffset`, but `readTitle()` always seeks to literal `0x38` and `readAuthor()` always seeks to `0xB8`. `open()` never uses or bounds-checks `m_header.metadataOffset` for metadata. A container whose metadata is not immediately after the 56-byte header therefore gets unrelated bytes, empty strings, or truncated data reported as its title/author even though the header identifies the real block.
- **Likely root cause:** The parser hard-coded the common contiguous layout instead of using the container's explicit metadata location.
- **Impact:** Relocated/reordered XTC metadata yields incorrect book identity in the reader, Recent Books, Home, and screenshot naming even though page data can load normally.
- **Repair direction:** Validate the advertised metadata region against file bounds, seek to `metadataOffset`, and read title/author relative to that block with exact read checks. Add tests for default placement, relocated metadata, and truncated/out-of-bounds metadata.

- **Consolidation sources:** [automation/bug-scan-20260929-0923](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/61e0308885e3d82d10013b44c0cc25885984bb8c/bugs.md); [Drive: 2026-09-29 09-23 MDT - automation-bug-scan-20260929-0923 - Instructions](https://docs.google.com/document/d/16-a2i6BOru1O8pdLvMa6KMQ7DHpzVTz_T5ZLxhspYIw/edit?usp=drivesdk); [Drive: 2026-09-29 09-23 MDT - automation-bug-scan-20260929-0923 - Diff](https://docs.google.com/document/d/1blE03USfMDKdOZCAsm-rzhy6EvDcdVGIFZ6T6cmGxjU/edit?usp=drivesdk)

### 180. A transient settings read failure quarantines or deletes a valid settings.json

- **Status:** Open.

- **Affected code:** `src/CrossPointSettings.cpp`, `CrossPointSettings::loadFromFile()` and `quarantineSettingsJson()`; `lib/hal/HalStorage.cpp`, `HalStorage::readFile()` and `openFileForReadUnlocked()`.
- **Trigger / reproduction:** Start with a valid `/.crosspoint/settings.json`. During boot, allow `Storage.exists(SETTINGS_FILE_JSON)` to succeed but inject a transient failure when the subsequent `sd.open(..., O_RDONLY)` runs, or inject a mid-read I/O failure that leaves an empty/malformed partial `String`.
- **Observed / logically demonstrated failure:** `HalStorage::readFile()` represents an open failure as an empty string and does not expose a distinct I/O-error result. `CrossPointSettings::loadFromFile()` treats any empty or unparsable result from an existing path as corruption and immediately calls `quarantineSettingsJson()`. That routine renames the canonical file to a backup when possible; if the rename fails, it explicitly deletes the canonical file. A valid settings file can therefore be retired solely because one read attempt failed, after which boot proceeds from migration/default state instead of retrying the intact configuration.
- **Likely root cause:** The settings loader conflates storage-read failure with validated content corruption, and destructive quarantine is performed without first proving that the bytes on disk are actually malformed.
- **Impact:** A transient SD/open/read fault during boot can make persistent device configuration disappear from its canonical location and cause defaults or older migration state to take effect. If the quarantine rename also fails, the code attempts deletion of the only valid settings file.
- **Repair direction:** Read settings through an API that distinguishes not-found, I/O failure, and complete byte content. Quarantine only after a complete successful read has been parsed and proven malformed. On open/read failure, leave the canonical file untouched and return/retry an explicit storage error. Add fault-injection tests for exists-then-open failure, short/mid-read failure, genuinely malformed JSON, and successful load.

- **Consolidation sources:** [automation/bug-scan-20260929-1019](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b9f31ac542f22a2125728b5afb03c429dc25901f/bugs.md); [Drive: 2026-09-29 10-19 MDT - automation-bug-scan-20260929-1019 - Instructions](https://docs.google.com/document/d/1R_ERc5NpUpiGYHoP5R_C2m2AAc-67yRdVgPE7N1qNtg/edit?usp=drivesdk); [Drive: 2026-09-29 10-19 MDT - automation-bug-scan-20260929-1019 - Diff](https://docs.google.com/document/d/1B9xor6sVkT6qu844DAU2HMPmrL0KxwPN2e_m5AUH6zA/edit?usp=drivesdk)

### 181. Failed EPUB deletion can erase the book's cache and reading progress while leaving the book itself intact

- **Status:** Open.

- **Affected code:** `src/native/NativeFileBrowserBridge.cpp`, `deleteDocument()`; `lib/Epub/Epub.cpp`, `Epub::clearCache()`; reader progress stored below `Epub::getCachePath()`, including `src/activities/reader/EpubReaderActivity.cpp`'s `progress.bin` load path.
- **Trigger / reproduction:** Open an EPUB so it has a populated `/.crosspoint/epub_<hash>/` cache/progress directory. Delete that EPUB from File Browser while injecting a failure in the final `Storage.remove(path)` call after cache removal succeeds (for example a transient SD removal/write error).
- **Observed / logically demonstrated failure:** For non-directory EPUBs, `deleteDocument()` calls `Epub(...).clearCache()` first and ignores its result, then calls `Storage.remove(path)`. If the file removal fails, the function returns `false` but the cache directory has already been recursively removed. The original EPUB remains on SD, yet its metadata/section/cover cache and reader progress have been destroyed. Reopening the still-present book therefore rebuilds cache state instead of resuming from the prior persisted progress.
- **Likely root cause:** Destructive dependent cleanup is ordered before the authoritative object deletion and is not transactional or rollback-capable.
- **Impact:** A delete operation that visibly reports failure can still cause irreversible loss of reading-state/cache data for a book that was not deleted. The operation's failure semantics are therefore unsafe: “failed” does not mean “no change.”
- **Repair direction:** Remove or transactionally move the EPUB first, and clear its derived cache only after the source deletion is known to have succeeded. Prefer staging the cache for deferred cleanup if rollback is needed. Propagate cache-cleanup failure separately without sacrificing the book. Add a regression where final file removal fails and verify the EPUB and its progress/cache remain intact.

- **Consolidation sources:** [automation/bug-scan-20260929-1019](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b9f31ac542f22a2125728b5afb03c429dc25901f/bugs.md); [Drive: 2026-09-29 10-19 MDT - automation-bug-scan-20260929-1019 - Instructions](https://docs.google.com/document/d/1R_ERc5NpUpiGYHoP5R_C2m2AAc-67yRdVgPE7N1qNtg/edit?usp=drivesdk); [Drive: 2026-09-29 10-19 MDT - automation-bug-scan-20260929-1019 - Diff](https://docs.google.com/document/d/1B9xor6sVkT6qu844DAU2HMPmrL0KxwPN2e_m5AUH6zA/edit?usp=drivesdk)

### 182. Package Manager reports a successful installed-package refresh after directory scan failures and publishes a partial inventory

- **Status:** Open.

- **Affected code:** `src/native/NativePackageManagerBridge.cpp`, `refreshInstalled()`, `clearInstalledCache()`, `installedCount()`, and `installedGet()`; consumer `Apps/package_manager.c::refresh()`.
- **Trigger / reproduction:** Install managed packages in more than one root such as `/Apps` and `/Drivers`. Open Package Manager while injecting an SD open/read failure for one installed-package root, or make `openNextFile()` fail part-way through a populated root while other roots remain readable.
- **Observed / logically demonstrated failure:** `refreshInstalled()` clears the previously valid global inventory before scanning. If a root cannot be opened or is not returned as a directory, the function silently `continue`s; if `openNextFile()` stops early, that condition is indistinguishable from end-of-directory. After scanning whatever remains, the bridge unconditionally returns `true`. `Apps/package_manager.c::refresh()` therefore accepts `installed_count()` as authoritative and renders the incomplete list with no storage-error status. Installed packages in the failed portion simply disappear from management until a later successful refresh.
- **Likely root cause:** Enumeration errors are treated as normal absence/end-of-directory, and the live cache is cleared/published incrementally instead of building a temporary snapshot that is committed only after a complete scan.
- **Impact:** Transient SD faults can make installed apps, drivers, services, or providers vanish from Package Manager even though they remain installed. The user cannot inspect/update/uninstall the hidden packages and can be misled about actual installed state. This is distinct from the existing 64-row UI-capacity bug: it occurs below the limit and is driven by scan failure rather than truncation.
- **Repair direction:** Build the installed inventory in temporary storage, distinguish missing optional roots from I/O/open/enumeration failures, and publish it only after every existing root has been scanned successfully. Preserve the previous valid snapshot or expose an explicit refresh failure instead of returning success with partial data. Add tests for root-open failure and mid-enumeration failure with packages before and after the fault.

- **Consolidation sources:** [automation/bug-scan-20260929-1019](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b9f31ac542f22a2125728b5afb03c429dc25901f/bugs.md); [automation/bug-scan-20260930-0324](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/61f98d60fa4eda874405b00c72b9a576e4ca9924/bugs.md); [Drive: 2026-09-30 0324 MDT - automation_bug-scan-20260930-0324 - Diff](https://docs.google.com/spreadsheets/d/1353mBOSb02ehViW2gfxo_G4Bsw-FXIdsTVKBs4qvn7g/edit?usp=drivesdk); [Drive: 2026-09-29 10-19 MDT - automation-bug-scan-20260929-1019 - Instructions](https://docs.google.com/document/d/1R_ERc5NpUpiGYHoP5R_C2m2AAc-67yRdVgPE7N1qNtg/edit?usp=drivesdk); [Drive: 2026-09-29 10-19 MDT - automation-bug-scan-20260929-1019 - Diff](https://docs.google.com/document/d/1B9xor6sVkT6qu844DAU2HMPmrL0KxwPN2e_m5AUH6zA/edit?usp=drivesdk); [Drive: 2026-09-30 0324 MDT - automation_bug-scan-20260930-0324 - Instructions](https://docs.google.com/spreadsheets/d/1rMBCmHgaBRqtd-LRVm6Fvx8mkcjYuqjssDPGuXNM0yk/edit?usp=drivesdk)

### 183. KOReader Sync accepts schema-invalid HTTP 200 progress bodies as valid remote positions

- **Status:** Open.

- **Affected code:** `lib/KOReaderSync/KOReaderSyncClient.cpp`, `KOReaderSyncClient::getProgress()`; `src/activities/reader/KOReaderSyncActivity.cpp`, `performSync()`; `lib/KOReaderSync/ProgressMapper.cpp`, `ProgressMapper::toCrossPoint()`.
- **Trigger / reproduction:** Have the configured KOReader-compatible server return HTTP 200 with syntactically valid JSON that omits or mistypes required progress fields, for example `{}`, `{"progress":null}`, or a body without a numeric `percentage`.
- **Observed / logically demonstrated failure:** `getProgress()` checks only that JSON parsing succeeds. It then reads `progress` with `.as<std::string>()` and `percentage` with `.as<float>()` without checking field presence or type, and returns `OK`. Missing values therefore become an empty XPath and 0%. `performSync()` marks the response as remote progress, maps it through `toCrossPoint()` (which resolves the zero/default values to a plausible position at the beginning of the book), renders it as a real remote checkpoint, and allows the user to apply it.
- **Likely root cause:** Transport success and JSON syntax validity are treated as sufficient proof of the KOReader progress response schema. ArduinoJson's default conversions hide missing/wrong-type fields instead of surfacing them as protocol errors.
- **Impact:** A proxy/server regression, truncated-but-still-valid JSON object, or incompatible KOReader endpoint can be presented as legitimate 0% remote progress. Applying it can unexpectedly jump the local reader to the start of the book instead of reporting a sync/protocol failure.
- **Repair direction:** Validate the HTTP-200 body before publishing `outProgress`: require the protocol's mandatory fields with the expected types, require a finite numeric percentage in the valid range, and reject missing/invalid progress data with `JSON_ERROR` or a dedicated protocol error. Parse into a temporary object and assign `outProgress` only after complete validation. Add tests for empty objects, null/wrong-type fields, non-finite/out-of-range percentages, and a valid response.

- **Consolidation sources:** [automation/bug-scan-20260929-1126](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b99d1c80c4201e399366f9d7d7bf685655a79655/bugs.md); [Drive: 2026-09-29 1126 MDT - automation-bug-scan-20260929-1126 - Instructions](https://docs.google.com/document/d/10MhueVepOY43NRSwVh6FkJecKy6xzQreFVj9GEyzah0/edit?usp=drivesdk); [Drive: 2026-09-29 1126 MDT - automation-bug-scan-20260929-1126 - Diff](https://docs.google.com/spreadsheets/d/1tileMLV2UkvBPsJcKLms1yet2ggp9mwPW2CYX4BcQgo/edit?usp=drivesdk)

### 184. Reader progress checkpoints are destructively truncated and short writes are reported as successful

- **Status:** Open.

- **Affected code:** `src/activities/reader/EpubReaderActivity.cpp::saveProgress()`, `src/activities/reader/TxtReaderActivity.cpp::saveProgress()`, `src/activities/reader/XtcReaderActivity.cpp::saveProgress()`; `lib/hal/HalStorage.cpp::openFileForWriteUnlocked()`; corresponding reader progress-load paths.
- **Trigger / reproduction:** Start with a valid `progress.bin`, turn a page so the reader saves a new checkpoint, and inject an SD short write, media error, or power interruption after `openFileForWrite()` succeeds but before all 4/6 progress bytes are durably written.
- **Observed / logically demonstrated failure:** `openFileForWriteUnlocked()` opens the canonical checkpoint with `O_TRUNC`, destroying the previous valid checkpoint immediately. All three `saveProgress()` implementations ignore the return value from `f.write()`; EPUB even logs “Progress saved” unconditionally after the unchecked write. The loaders require an exact 4-byte TXT/XTC record or a 4/6-byte EPUB record, so a partial replacement is later ignored and the reader falls back to its default/first-page behavior. A failed save can therefore erase the last known-good resume position while being treated as success.
- **Likely root cause:** Tiny resume records are written directly in place with no transactional staging, and the write/flush/close result is not part of the save contract.
- **Impact:** A transient SD fault or power loss during an ordinary page render can lose reading position for EPUB, TXT/Markdown, and XTC books. Because progress is saved frequently, this exposes a durable user-state file to repeated destructive replacement.
- **Repair direction:** Write the complete checkpoint to a same-directory temporary file, verify the exact byte count plus sync/close success, then atomically replace the canonical `progress.bin` while retaining the old checkpoint until publication succeeds. At minimum, make each save path check exact writes and report failure; add fault-injection tests proving the previous checkpoint survives open/write/sync failures and that partial files are never published.

- **Consolidation sources:** [automation/bug-scan-20260929-1126](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/b99d1c80c4201e399366f9d7d7bf685655a79655/bugs.md); [Drive: 2026-09-29 1126 MDT - automation-bug-scan-20260929-1126 - Instructions](https://docs.google.com/document/d/10MhueVepOY43NRSwVh6FkJecKy6xzQreFVj9GEyzah0/edit?usp=drivesdk); [Drive: 2026-09-29 1126 MDT - automation-bug-scan-20260929-1126 - Diff](https://docs.google.com/spreadsheets/d/1tileMLV2UkvBPsJcKLms1yet2ggp9mwPW2CYX4BcQgo/edit?usp=drivesdk)

### 185. Failed legacy-state retirement can later roll current state back to stale binary data

- **Status:** Open.

- **Affected code:** `src/CrossPointState.cpp`, especially `CrossPointState::loadFromFile()`; state persistence through `src/JsonSettingsIO.cpp::saveState()` / `loadState()`.
- **Trigger / reproduction:** Start with a valid legacy `/.crosspoint/state.bin` and no JSON state. Let binary loading and the subsequent `state.json` save succeed, but force `Storage.rename("/.crosspoint/state.bin", "/.crosspoint/state.bin.bak")` to fail. Continue using the device so `state.json` advances beyond the legacy state. On a later boot or reload, make the existing `state.json` read return empty/unreadable.
- **Observed / logically demonstrated failure:** `loadFromFile()` ignores the return value of the legacy-file rename, logs that migration succeeded, and returns `true`. The stale `state.bin` therefore remains a live fallback. If the current JSON later cannot be read, the loader falls through to that old binary file, loads its older `openEpubPath`, sleep-history fields, reader load count, and `lastSleepFromReader`, then can successfully call `saveToFile()` and overwrite the JSON state with those stale values.
- **Likely root cause:** Migration success is decided after publishing the replacement JSON but before verifying retirement of the legacy fallback source. The loader also treats an existing-but-unreadable JSON state the same as an absent JSON state and therefore permits fallback to any leftover legacy file.
- **Impact:** A transient SD failure during migration followed by a later JSON read failure can convert a recoverable I/O problem into persistent state rollback, losing newer reader/session and sleep-selection state.
- **Repair direction:** Treat legacy retirement as part of the migration transaction: check the rename result, handle an existing backup safely, and do not report migration complete while the legacy source remains eligible for fallback. Distinguish "JSON absent" from "JSON exists but could not be read/parsed" so a stale legacy file is not automatically preferred after migration. Add a fault-injection test for rename failure followed by a later JSON read failure and prove newer state is never overwritten by the old binary snapshot.

- **Consolidation sources:** [automation/bug-scan-20260929-1227](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/6e24332a2bf14ad272bb4b8d7944e5e9a9cd1e1c/bugs.md); [Drive: 2026-09-29 1227 MDT - automation-bug-scan-20260929-1227 - Instructions](https://docs.google.com/document/d/1W1g8tZYLjNvZt4NIm61ESqN6j60PCManQaqLmS_7yj0/edit?usp=drivesdk); [Drive: 2026-09-29 1227 MDT - automation-bug-scan-20260929-1227 - Diff](https://docs.google.com/document/d/1fZ8vZ0C1ZjmKnHULiYYQSDc1Cg7AqNSix4w-kpxSVwY/edit?usp=drivesdk)

### 186. Ask Manifold can erase a valid saved conversation after one transient session read failure

- **Status:** Open.

- **Affected code:** `Apps/llm_ask.c`, especially `load_session()`, `request_keyboard()`, and `app_main()`.
- **Trigger / reproduction:** Start with a valid saved Ask Manifold conversation in the session file. Launch the app while injecting a one-time storage read failure for that file, then press Ask to open the keyboard.
- **Observed / logically demonstrated failure:** `load_session()` treats any failed or short `read_file()` like invalid session data and immediately calls `reset_session()`. `app_main()` ignores the false return and continues. When the user presses Ask, `request_keyboard()` calls `save_session()` before requesting the keyboard, so the reset empty in-memory session is written over the previously valid durable conversation even though the only problem was a transient read failure.
- **Likely root cause:** The loader conflates missing/corrupt data with transient I/O failure, and a later handoff save commits the fallback state without proving that the previous durable state was invalid.
- **Impact:** A temporary SD read fault can permanently erase the user's saved conversation merely by pressing Ask after launch.
- **Repair direction:** Distinguish not-found, corrupt, and I/O-failure outcomes. On I/O failure, keep the session unavailable/read-only and do not overwrite the existing file until it has been successfully loaded or the user explicitly chooses to reset it. Add a fault-injection test where the first read fails, the user requests the keyboard, and the original session remains byte-for-byte intact.

- **Consolidation sources:** [automation/bug-scan-20260929-1325-final](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/a2a96bbe6fd61e31339ebf6cd83080c9375ff533/bugs.md); [automation/bug-scan-20260929-1325](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/339a210c68dcca5efda472896943769c30581343/bugs.md); [Drive: 2026-09-29 1325 automation-bug-scan-20260929-1325-final Instructions](https://docs.google.com/spreadsheets/d/1YbJE6YPRZsBGC54dWnNamiLrLVwPHwvdp9kURLmtrQg/edit?usp=drivesdk); [Drive: 2026-09-29 1325 automation-bug-scan-20260929-1325-final Diff](https://docs.google.com/spreadsheets/d/12U7ZUH2XA16WgTIMC70fphq9fk2KesQeSGNgcP3NVE0/edit?usp=drivesdk)

### 187. A failed OPDS book re-download can destroy the existing local EPUB with the same generated filename

- **Status:** Open.

- **Affected code:** `src/activities/browser/OpdsBookBrowserActivity.cpp::downloadBook()`; `src/network/HttpDownloader.cpp::downloadToFile()`.
- **Trigger / reproduction:** Start with an existing local book whose pathname matches the deterministic OPDS destination, such as `/Author - Title.epub`. Download the same catalog title again, allow the HTTP request to reach a 200 response, then interrupt the transfer or inject an SD/network failure before the body completes.
- **Observed / logically demonstrated failure:** `downloadBook()` writes directly to the final sanitized `.epub` pathname. The compatibility path in `downloadToFile()` removes any existing destination before opening the replacement. If streaming, length validation, or a later write then fails, the downloader removes the partial destination and returns an error. The previously valid book has already been deleted, so a failed re-download leaves no copy at all.
- **Likely root cause:** User content is downloaded using destructive overwrite semantics instead of staging a replacement and publishing it only after transfer validation succeeds.
- **Impact:** A transient network disconnect, server truncation, SD write failure, or cancellation during an OPDS re-download can irreversibly delete an existing EPUB from the user's library.
- **Repair direction:** Download to a unique same-directory staging file, validate the completed transfer and preferably basic EPUB/ZIP structure, then replace the destination transactionally while preserving or rolling back the old file on publication failure. Add a fault-injection test proving an existing EPUB survives failed re-downloads at each post-200 transfer stage.

- **Consolidation sources:** [automation/bug-scan-20260929-1325-final](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/a2a96bbe6fd61e31339ebf6cd83080c9375ff533/bugs.md); [automation/bug-scan-20260929-1325](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/339a210c68dcca5efda472896943769c30581343/bugs.md); [Drive: 2026-09-29 1325 automation-bug-scan-20260929-1325-final Instructions](https://docs.google.com/spreadsheets/d/1YbJE6YPRZsBGC54dWnNamiLrLVwPHwvdp9kURLmtrQg/edit?usp=drivesdk); [Drive: 2026-09-29 1325 automation-bug-scan-20260929-1325-final Diff](https://docs.google.com/spreadsheets/d/12U7ZUH2XA16WgTIMC70fphq9fk2KesQeSGNgcP3NVE0/edit?usp=drivesdk)

### 188. A transient SD font-directory scan failure can unload the active font and clear its selection in RAM

- **Status:** Open.
- **Affected code:** `lib/EpdFont/SdCardFontRegistry.cpp::discover()`, `scanRoot()`, and `scanDirectory()`; `src/SdCardFontSystem.cpp::begin()` and `ensureLoaded()`.
- **Trigger / reproduction:** Select an SD-card font family, then force a transient SD open/enumeration failure while the registry is rediscovered (for example after `markRegistryDirty()` following a font upload/delete, or during boot). Fail opening the root/family directory or make `openNextFile()` stop early even though the family still exists on the card.
- **Observed / logically demonstrated failure:** `discover()` clears `families_` before scanning. The scan helpers return `void` and treat both directory-open failure and an invalid `openNextFile()` as ordinary absence/end-of-directory, so the caller cannot distinguish a complete scan from an I/O failure. `ensureLoaded()` consumes that partial/empty snapshot, fails to find the still-installed active family, unloads it, and sets `SETTINGS.sdFontFamilyName[0] = '\0'`. Because the dirty flag was already cleared, the selection does not automatically recover when the SD card becomes readable again; a later unrelated settings save can also make the cleared selection durable.
- **Likely root cause:** Font discovery destructively replaces the authoritative registry without transactional scan success/error reporting, and the font system interprets “not present in this snapshot” as confirmed removal.
- **Impact:** A transient SD fault can unexpectedly switch the reader back to a built-in font and lose the user's active-family selection for the session, with a path to persisting that loss.
- **Repair direction:** Build discovery into a temporary registry, distinguish confirmed EOF/not-found from I/O/enumeration failure, and swap it into `families_` only after a complete successful scan. On scan failure retain the last known-good registry and active selection and leave the dirty flag set for retry. Add fault-injected root-open and mid-enumeration tests.

- **Consolidation sources:** [automation/bug-scan-20260929-1521](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/3b5ee347c5f261f42ffb47c4e42fa8d6938d0b1c/bugs.md); [Drive: 2026-09-29 1521 MDT - automation-bug-scan-20260929-1521 - Diff](https://docs.google.com/spreadsheets/d/1cTBgkQqcH5XClkBpjrNyYB1r77lq226fvwpP8SzDBVo/edit?usp=drivesdk); [Drive: 2026-09-29 1521 MDT - automation-bug-scan-20260929-1521 - Diff](https://docs.google.com/document/d/1n7GGQT0HAu48H5Kslu1if8a5qKqt15oE2-9WHM0l_sA/edit?usp=drivesdk); [Drive: 2026-09-29 1521 MDT - automation-bug-scan-20260929-1521 - Instructions](https://docs.google.com/spreadsheets/d/1Ip2qZ7MWIuO0ktAYH7PbAvU1eU84E53kkBOAr1iYdcs/edit?usp=drivesdk)

### 189. Malformed cpfont glyph lengths can make text rendering read beyond the loaded bitmap buffer

- **Status:** Open.
- **Affected code:** `lib/EpdFont/SdCardFont.cpp::load()` and `prewarmStyle()`; `lib/EpdFont/EpdFontData.h::EpdGlyph`; `lib/GfxRenderer/GfxRenderer.cpp::renderCharImpl()`.
- **Trigger / reproduction:** Install or place a structurally loadable `.cpfont` whose interval table points to a glyph record with valid-looking metadata but a `dataLength` smaller than the bitmap implied by `width`, `height`, and the file's 1-bit/2-bit mode—for example a 255×255 1-bit glyph declaring `dataLength = 1`. Render text containing that codepoint.
- **Observed / logically demonstrated failure:** The font loader validates top-level counts and interval layout but does not cross-check each `EpdGlyph::dataLength` against its dimensions or prove `dataOffset + dataLength` lies inside the bitmap section. `prewarmStyle()` therefore allocates/copies only the declared `dataLength` bytes and publishes the glyph. `renderCharImpl()` ignores `dataLength` and iterates `width * height` pixels, indexing `bitmap[pixelPosition >> 3]` for 1-bit fonts or `>> 2` for 2-bit fonts. The example allocates one bitmap byte but the renderer can read thousands of bytes beyond it.
- **Likely root cause:** On-disk glyph metadata is treated as internally consistent; loading validates exact I/O counts but not the semantic relationship between bitmap geometry, length, offsets, and file bounds.
- **Impact:** A corrupted, manually copied, or malicious cpfont can cause deterministic out-of-bounds heap reads during ordinary text rendering, potentially crashing the reader or exposing adjacent memory to rendering logic.
- **Repair direction:** Validate every glyph record before it can enter a mini/persistent/overflow cache: compute the exact required bitmap bytes from dimensions and bit depth using checked arithmetic, require the declared length to match the format contract, and bounds-check every bitmap range against the actual file size/section. Reject the whole font on any inconsistency and add malformed-glyph fixtures for short length, oversized offset, and range overflow.

- **Consolidation sources:** [automation/bug-scan-20260929-1521](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/3b5ee347c5f261f42ffb47c4e42fa8d6938d0b1c/bugs.md); [Drive: 2026-09-29 1521 MDT - automation-bug-scan-20260929-1521 - Diff](https://docs.google.com/spreadsheets/d/1cTBgkQqcH5XClkBpjrNyYB1r77lq226fvwpP8SzDBVo/edit?usp=drivesdk); [Drive: 2026-09-29 1521 MDT - automation-bug-scan-20260929-1521 - Diff](https://docs.google.com/document/d/1n7GGQT0HAu48H5Kslu1if8a5qKqt15oE2-9WHM0l_sA/edit?usp=drivesdk); [Drive: 2026-09-29 1521 MDT - automation-bug-scan-20260929-1521 - Instructions](https://docs.google.com/spreadsheets/d/1Ip2qZ7MWIuO0ktAYH7PbAvU1eU84E53kkBOAr1iYdcs/edit?usp=drivesdk)

### 190. Native app allocator cleanup timeout poisons all later native-app launches

- **Status:** Open.

- **Affected code:** `src/native/NativeAppMemory.cpp`, especially `native_app_memory_end()` and `native_app_memory_begin()`; `lib/NativeApps/src/NativeAppLauncher.c::launch_elf_app()` cleanup paths.
- **Trigger / reproduction:** Run a native app that has another task using the app allocation ledger and make that task hold the allocator mutex longer than `native_app_memory_end()`'s roughly 100 ms lock timeout as `app_main()` returns. Equivalently, fault-inject the cleanup `xSemaphoreTake()` to fail once. Then launch any native app again.
- **Observed / logically demonstrated failure:** On the timeout, `native_app_memory_end()` logs `Allocator busy during exit; retaining invocation` and returns without clearing the global `entries` pointer or ending the ledger. Its return type is `void`, so `launch_elf_app()` cannot detect the failed cleanup: it immediately sets `memory_active = false` and continues module teardown. On the next launch, `native_app_memory_begin()` sees `entries` still non-null and returns `false`, which the launcher reports as `ESP_ERR_NO_MEM`. Every later native-app launch therefore fails until reboot even if plenty of memory is actually free.
- **Likely root cause:** Allocator teardown is fallible, but its API hides that failure and the ELF launcher unconditionally publishes the allocator as inactive.
- **Impact:** One teardown-time mutex contention can strand the entire native-app subsystem for the remainder of the boot, while also retaining the previous invocation's tracked allocations. Continuing to unload after failed allocator quiescence also weakens the intended lifetime boundary for app-owned worker activity.
- **Repair direction:** Make allocator teardown return an explicit result and do not clear `memory_active`, unload the ELF, or release the launch guard until the ledger is safely quiesced. Join/stop app-owned allocator users before teardown or fail closed by retaining the mapped generation and requiring a controlled restart. Add a regression that forces the first cleanup lock acquisition to time out and proves a subsequent app can launch without stale ledger state.

- **Consolidation sources:** [automation/bug-scan-20260929-1626-final](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/54284a443204671d3171ee3a284a3188050b7cc8/bugs.md); [automation/bug-scan-20260930-0419](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/8f3a068b9f2d6105e008255713ee5086691a134a/bugs.md); [Drive: 2026-09-30 0419 MDT - automation_bug-scan-20260930-0419 - Diff](https://docs.google.com/spreadsheets/d/1o6Bfq-I4ygMn_Ai6dZFXLxodBuN4p08HsGPgkuWaXGo/edit?usp=drivesdk); [Drive: 2026-09-29 16-26 MDT - automation-bug-scan-20260929-1626-final - bugs.md diff](https://docs.google.com/document/d/1uX1u-8YzcZ4g9PDQj0dsg8rzuAq8E3DsfrNMKoc6Rv8/edit?usp=drivesdk); [Drive: 2026-09-29 16-26 MDT - automation-bug-scan-20260929-1626-final - integration instructions](https://docs.google.com/document/d/10A9u-snYf3GwuAYWrTAZF89FS4-wDHPcXGhIMMG2XZg/edit?usp=drivesdk); [Drive: 2026-09-30 0419 MDT - automation_bug-scan-20260930-0419 - Instructions](https://docs.google.com/spreadsheets/d/1PyHqC8xFRzQs02Mt1-ory7jR4LOiSeqVqJ1dFvpgQzg/edit?usp=drivesdk)

### 191. A failed managed-app launch leaks its package-use pin and can permanently block update or uninstall

- **Status:** Open.

- **Affected code:** `src/native/NativeAppHost.cpp::runNativeApp()`; `lib/NativeApps/src/NativeAppLauncher.c::launch_elf_app()`; `src/runtime/packages/PackageUseGate.h`.
- **Trigger / reproduction:** Launch a canonical managed app from `/Apps/<id>/<artifact>.elf` and make `launch_elf_app()` fail before a module needs to be retained—for example by making a required capability unavailable, making `native_app_memory_begin()` fail, or supplying an ELF that fails `dlopen()`. Then try to update or uninstall that package. Repeat the failed launch to show the pin count accumulating.
- **Observed / logically demonstrated failure:** `runNativeApp()` pins the canonical package root before calling `launch_elf_app()`, but it calls `unpin()` only when the returned `esp_err_t` is exactly `ESP_OK`. Many non-OK exits occur before the ELF is mapped at all, or after it has been safely closed, yet those paths leave the pin in `PackageUseGate`. `beginReplacement()` refuses any package with outstanding pins, so a harmless launch failure can make later package replacement/uninstall fail for the rest of the boot; repeated failed launches increment the leaked pin count.
- **Likely root cause:** The caller uses overall launch success as a proxy for module-lifetime disposition. A non-OK launch result is not equivalent to a failed `dlclose()` or an intentionally retained module.
- **Impact:** A missing capability, malformed app, transient allocation failure, or similar launch error can prevent recovery by installing a fixed version of that same managed app until the device is rebooted.
- **Repair direction:** Return an explicit mapped/unloaded/retained disposition from the ELF launcher, or wrap the pin in an RAII lease that releases on every path except a verified failed-unload/retained-module case. Add tests for pre-`dlopen` failure, post-`dlopen` successful cleanup with a non-OK app result, and true failed-unload retention.

- **Consolidation sources:** [automation/bug-scan-20260929-1626-final](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/54284a443204671d3171ee3a284a3188050b7cc8/bugs.md); [Drive: 2026-09-29 16-26 MDT - automation-bug-scan-20260929-1626-final - bugs.md diff](https://docs.google.com/document/d/1uX1u-8YzcZ4g9PDQj0dsg8rzuAq8E3DsfrNMKoc6Rv8/edit?usp=drivesdk); [Drive: 2026-09-29 16-26 MDT - automation-bug-scan-20260929-1626-final - integration instructions](https://docs.google.com/document/d/10A9u-snYf3GwuAYWrTAZF89FS4-wDHPcXGhIMMG2XZg/edit?usp=drivesdk)

### 192. Clock sync can write UTC to the RTC without durably recording the UTC storage mode

- **Status:** Open.

- **Affected code:** `src/ClockSync.cpp::commitCurrentSystemTime()`; `lib/hal/HalClock.cpp::syncRtcFromSystemTime()`, `HalClock::configure()`, and `HalClock::syncSystemTimeFromRtc()`; settings persistence through `CrossPointSettings::saveToFile()`.
- **Trigger / reproduction:** Start in a non-UTC timezone with persisted `rtcStoresUtc == 0` (legacy/local RTC mode). Acquire valid system time and allow `syncRtcFromSystemTime()` to successfully write UTC fields to the RTC, but fault-inject `SETTINGS.saveToFile()` to fail. Reboot before any later successful settings save.
- **Observed / logically demonstrated failure:** `syncRtcFromSystemTime()` always writes the RTC from `gmtime_r()`, so after it succeeds the hardware clock contains UTC. `commitCurrentSystemTime()` only then changes `SETTINGS.rtcStoresUtc` to 1, updates the variant/reference metadata, and attempts to save. If that save fails, the code merely logs the error, configures the live clock for UTC anyway, and returns success. The durable settings still describe the RTC as local time. On the next boot, `HalClock::configure()` therefore starts in local-storage mode; when both UTC and local interpretations are plausible and the old reference cannot disambiguate them, `syncSystemTimeFromRtc()` keeps the persisted local mode and interprets the UTC fields as local wall time, shifting the recovered epoch by the timezone offset.
- **Likely root cause:** The RTC representation migration and its persistent format metadata are committed as two independent operations, and persistence failure is not part of the clock-sync success predicate.
- **Impact:** A settings-write failure immediately after an otherwise successful network clock sync can make the next boot recover a system time hours early or late, despite the RTC itself containing the correct UTC fields.
- **Repair direction:** Treat RTC format and its durable mode metadata as one transaction. Do not report sync success until the UTC-mode metadata is durable; if metadata persistence fails after the hardware write, either restore the RTC to the previously declared representation or persist/recover an unambiguous migration marker before boot can consume it. Add a non-UTC regression with `rtcStoresUtc=0`, successful RTC write, failed settings save, and reboot-time recovery.

- **Consolidation sources:** [automation/bug-scan-20260929-1626-final](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/54284a443204671d3171ee3a284a3188050b7cc8/bugs.md); [Drive: 2026-09-29 16-26 MDT - automation-bug-scan-20260929-1626-final - bugs.md diff](https://docs.google.com/document/d/1uX1u-8YzcZ4g9PDQj0dsg8rzuAq8E3DsfrNMKoc6Rv8/edit?usp=drivesdk); [Drive: 2026-09-29 16-26 MDT - automation-bug-scan-20260929-1626-final - integration instructions](https://docs.google.com/document/d/10A9u-snYf3GwuAYWrTAZF89FS4-wDHPcXGhIMMG2XZg/edit?usp=drivesdk)

### 193. XTC page loading ignores declared page/data bounds and can read through one page into the next

- **Status:** Open.

- **Affected code:** `lib/Xtc/Xtc/XtcParser.cpp`, especially `readPageTableEntry()`, `loadPage()`, and `loadPageStreaming()`; `lib/Xtc/Xtc/XtcTypes.h`, `PageTableEntry::dataSize` and `XtgPageHeader::dataSize/compression/colorMode`.
- **Trigger / reproduction:** Create a two-page XTC/XTCH in which page 0 has a valid XTG/XTH magic and dimensions but declares a `PageTableEntry::dataSize` and/or embedded `XtgPageHeader::dataSize` smaller than the bitmap implied by those dimensions, with page 1 placed immediately afterward. Alternatively set `compression` nonzero while leaving enough following bytes in the file. Open page 0.
- **Observed / logically demonstrated failure:** `readPageTableEntry()` copies the table offset/size without validating that the page region is in file bounds. After checking only the page magic, both load paths recompute `bitmapSize` from width/height and read that many bytes. They never compare the read against the table's `dataSize`, the page header's `dataSize`, or the declared page extent, and they do not reject unsupported `compression` or `colorMode`. If the file contains enough following bytes, the read succeeds by consuming bytes belonging to the next page or another section and returns success; compressed payload bytes are likewise treated as raw pixels.
- **Likely root cause:** Page-table/header metadata is treated as advisory while the loader trusts only dimensions and the physical ability to read bytes from the file.
- **Impact:** Corrupt or crafted XTC files can display cross-page/header data as pixels instead of being rejected, defeat per-page corruption isolation, and make malformed page boundaries appear valid. Streaming has the same defect.
- **Repair direction:** Before reading payload data, use checked 64-bit arithmetic to prove `page.offset + sizeof(XtgPageHeader) + payload <= fileSize`; require supported `compression/colorMode`; require table dimensions and embedded dimensions to agree; and require table/header data-size fields to agree with the exact calculated payload size (or explicitly handle documented alternatives). Add fixtures with undersized/oversized table sizes, mismatched embedded sizes, nonzero compression, and adjacent pages.

- **Consolidation sources:** [automation/bug-scan-20260929-1721](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/990fb155e4153f52af78d43ee4a6020fc21d2b9f/bugs.md); [Drive: 2026-09-29_1721_MDT_no-PR_automation-bug-scan-20260929-1721_instructions](https://docs.google.com/spreadsheets/d/1B6jsqIdvTzuAJtVj_QpirPvKMi93crRUKinwVqmHxAs/edit?usp=drivesdk); [Drive: 2026-09-29_1721_MDT_no-PR_automation-bug-scan-20260929-1721_diff](https://docs.google.com/spreadsheets/d/1T0ewi9rDs-IC-vdvoFAHlkma1VR2j07_dIVGykhjkjE/edit?usp=drivesdk)

### 194. TXT/XTC BMP copy loops can make no forward progress forever after a zero-byte SD read

- **Status:** Open.

- **Affected code:** `lib/Txt/Txt.cpp`, `Txt::generateCoverBmp()` in the existing-BMP copy path; `lib/Xtc/Xtc.cpp`, `Xtc::generateThumbBmp()` in the no-scaling cover-copy path.
- **Trigger / reproduction:** Use a TXT book with an external BMP cover, or an XTC whose cover is copied directly to the thumbnail, and inject an SD read that returns 0 before EOF while the source file position remains unchanged. A short/failed destination write is a second trigger for silent cache corruption.
- **Observed / logically demonstrated failure:** Both copy paths use `while (src.available())` and then call `src.read(...)` followed by `dst.write(...)` without checking that the read advanced by at least one byte. A zero-byte read before EOF leaves the source position unchanged, so `available()` can remain nonzero and the loop repeats forever with zero-byte writes. Neither path checks destination write counts; TXT returns success after the loop, while XTC treats mere destination-path existence as success, so short writes can also leave a truncated cache accepted as valid.
- **Likely root cause:** The loops use `available()` as both EOF and I/O-error detection and do not enforce forward progress or exact writes.
- **Impact:** A transient SD read fault during cover/thumbnail generation can hang the reader task until watchdog/reset. SD-full or media-write faults can leave persistent truncated BMP caches that subsequent launches reuse because the cache file already exists.
- **Repair direction:** Copy against a known remaining byte count; treat any zero/short read before expected EOF as failure; require every destination write to match the requested size and require successful close/finalization. Publish through a temporary file renamed only after a complete copy. Add fake-file tests for zero-progress read, short read, short write, and close failure.

- **Consolidation sources:** [automation/bug-scan-20260929-1721](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/990fb155e4153f52af78d43ee4a6020fc21d2b9f/bugs.md); [Drive: 2026-09-29_1721_MDT_no-PR_automation-bug-scan-20260929-1721_instructions](https://docs.google.com/spreadsheets/d/1B6jsqIdvTzuAJtVj_QpirPvKMi93crRUKinwVqmHxAs/edit?usp=drivesdk); [Drive: 2026-09-29_1721_MDT_no-PR_automation-bug-scan-20260929-1721_diff](https://docs.google.com/spreadsheets/d/1T0ewi9rDs-IC-vdvoFAHlkma1VR2j07_dIVGykhjkjE/edit?usp=drivesdk)

### 195. BQ27220 provisioning failure can leave the fuel gauge unsealed in CFGUPDATE mode

- **Status:** Open.

- **Affected code:** `lib/bq27220/src/bq27220.cpp`, `BQ27220::init()` and `BQ27220::dateMemoryCheck()`; caller `lib/Board_T5S3/BoardT5S3.cpp::configureBq27220()`.
- **Trigger / reproduction:** Force any data-memory `parameterCheck(..., update=true)` operation to fail after `dateMemoryCheck()` has successfully issued `ENTER_CFG_UPDATE`, for example by fault-injecting one I2C write/read failure during gauge profile provisioning.
- **Observed / logically demonstrated failure:** `dateMemoryCheck()` sets `result = false` when a parameter operation fails, but `EXIT_CFG_UPDATE_REINIT` is executed only under `if (update && result)`. The function therefore returns false without attempting to leave CFGUPDATE. `init()`, which previously unsealed the gauge, then breaks out before `sealAccess()`. `configureBq27220()` responds by calling `bq27220.end()`, which removes the host-side I2C device handle but sends no recovery command to the physical gauge.
- **Likely root cause:** Hardware-state cleanup is conditional on the provisioning body succeeding instead of being guaranteed once configuration mode/unsealed access has been entered.
- **Impact:** A single transient I2C fault during provisioning can leave the BQ27220 in a special configuration state and unsealed after firmware abandons the device for the boot. Normal gauging/telemetry may remain unavailable or unreliable until a later reset/reinitialization, and the intended sealed state is not restored.
- **Repair direction:** Treat CFGUPDATE entry and unseal as scoped hardware transactions. On every exit after entry, attempt a deterministic exit/reinitialize (or reset fallback) and reseal before releasing the device; report cleanup failure separately. Add fault injection at each profile-write step and verify CFGUPDATE clears and the gauge is sealed afterward.

- **Consolidation sources:** [automation/bug-scan-20260929-1823](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/66efc35761d7ed6bc0cf53c7b151ba6533e03035/bugs.md); [Drive: 2026-09-29 1823 MDT - automation_bug-scan-20260929-1823 - Instructions](https://docs.google.com/spreadsheets/d/14Xk8HtTwY5BtOEJKKftF1HZCL0C3nEKCpxpxHUg2cmI/edit?usp=drivesdk); [Drive: 2026-09-29 1823 MDT - automation_bug-scan-20260929-1823 - Diff](https://docs.google.com/spreadsheets/d/1TPhDZw8-Vhfak_h1pT0hJdvu40fJKaskrXyf1AbUnas/edit?usp=drivesdk)

### 196. A truncated EPUB section cache is accepted as valid and later feeds uninitialized offsets into page deserialization

- **Status:** Open.

- **Affected code:** `lib/Epub/Epub/Section.cpp`, `Section::loadSectionFile()` and `Section::loadPageFromSectionFile()`; `lib/Serialization/Serialization.h::readPod(FsFile&, ...)`; downstream `lib/Epub/Epub/Page.cpp::Page::deserialize()`.
- **Trigger / reproduction:** Start from a valid section cache `.../sections/<spine>.bin` with at least one page and truncate it to the first 20 bytes: the complete version/render-parameter fields plus `pageCount`, but omit the three 32-bit LUT/anchor/paragraph offsets and all page data. Reopen the same book with matching render settings and navigate to that section.
- **Observed / logically demonstrated failure:** `loadSectionFile()` reads only through `pageCount`; it neither requires the full 32-byte section header nor validates the three stored offsets before returning success. The generic `serialization::readPod(FsFile&, T&)` discards the read byte count. `loadPageFromSectionFile()` then seeks to byte 20, attempts to read the missing `lutOffset` into an uninitialized local, seeks using that indeterminate value, repeats the unchecked read for `pagePos`, and finally calls `Page::deserialize()` on an arbitrary location. The cache is therefore classified as valid before its required navigation structures are proven present.
- **Likely root cause:** Section-cache validation checks configuration compatibility but not structural completeness, while the serialization helpers expose no read failure to callers.
- **Impact:** A short write, power loss, or SD corruption of a derived section cache can turn the next page load into arbitrary seeks, bogus allocation/deserialization input, crashes, or corrupted rendering instead of simply rebuilding the cache.
- **Repair direction:** Require an exact complete header read, validate `pageCount` and every offset/range against file size, make all section/Page deserialization reads checked, and invalidate/rebuild the cache on the first structural or I/O error. Publish newly generated section files atomically only after all writes and close succeed. Add truncation tests at every header/LUT boundary.

- **Consolidation sources:** [automation/bug-scan-20260929-1823](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/66efc35761d7ed6bc0cf53c7b151ba6533e03035/bugs.md); [Drive: 2026-09-29 1823 MDT - automation_bug-scan-20260929-1823 - Instructions](https://docs.google.com/spreadsheets/d/14Xk8HtTwY5BtOEJKKftF1HZCL0C3nEKCpxpxHUg2cmI/edit?usp=drivesdk); [Drive: 2026-09-29 1823 MDT - automation_bug-scan-20260929-1823 - Diff](https://docs.google.com/spreadsheets/d/1TPhDZw8-Vhfak_h1pT0hJdvu40fJKaskrXyf1AbUnas/edit?usp=drivesdk)

### 197. Rom Manager can successfully import a direct .gb under an extensionless truncated filename and then hide it

- **Status:** Open.

- **Affected code:** `Apps/rom_manager.c`, especially `NAME_CAP`, `safe_name()`, `ensure_gb_suffix()`, `import_url()`, `make_path()`, and `load_roms()`.
- **Trigger / reproduction:** Import a direct HTTPS Game Boy URL whose basename is longer than the 127-character `NAME_CAP - 1` payload, for example a URL ending in 130 `a` characters followed by `.gb`.
- **Observed / logically demonstrated failure:** `import_url()` first accepts the full URL because it ends in `.gb`. `safe_name()` then truncates the basename to 127 characters, which can remove the extension. `ensure_gb_suffix()` silently returns when there is no room to append three more characters. The subsequent rename can succeed, and the app reports the ROM as imported, but `load_roms()` later lists only filenames ending in `.gb`; the imported file is therefore invisible and unmanageable in Rom Manager.
- **Likely root cause:** Filename truncation does not reserve space for the mandatory extension, and suffix restoration has a void/no-error failure path that is not validated before publication.
- **Impact:** A nominally successful import can consume storage while disappearing from the app's ROM list, forcing manual file cleanup and preventing launch/rename/delete through Rom Manager.
- **Repair direction:** Derive the output filename from the URL path, reserve `.gb` plus the terminator before truncating the stem, and fail the import if a valid final name cannot be produced. Revalidate that the final name ends in `.gb` before renaming the temporary download. Add boundary tests for basenames just below, at, and above `NAME_CAP`.

- **Consolidation sources:** [automation/bug-scan-20260929-2102](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/728e209d547481d1060b7841b4e22c5eb837ff8c/bugs.md); [Drive: 2026-09-29 21-02 MDT - automation-bug-scan-20260929-2102 - Instructions](https://docs.google.com/spreadsheets/d/10o78oxZAN0DYrcpzIBXxrHkgQLsr9hocvSpV1IfPSEY/edit?usp=drivesdk); [Drive: 2026-09-29 21-02 MDT - automation-bug-scan-20260929-2102 - Diff](https://docs.google.com/spreadsheets/d/1llLFetNI6nEmi67v1zvyKkLU9xAapNH61elrKCuOF8s/edit?usp=drivesdk)

### 198. Rom Manager rejects valid .gb and .zip HTTPS URLs that carry query strings or fragments

- **Status:** Open.

- **Affected code:** `Apps/rom_manager.c`, especially `import_url()`, `ends_ci()`, and the URL-to-filename handling in `safe_name()`.
- **Trigger / reproduction:** Try a valid direct ROM or archive URL such as `https://example.test/game.gb?token=abc`, `https://example.test/game.zip?download=1`, or a supported path followed by a fragment.
- **Observed / logically demonstrated failure:** `import_url()` decides the file type by applying `ends_ci()` to the entire serialized URL. Once a query string or fragment follows `.gb` or `.zip`, neither suffix check matches, so the function returns `URL must end in .zip or .gb` before opening the HTTP stream. This conflicts with `safe_name()`, which already stops filename extraction at `?` and `#`, showing that query/fragment-bearing URLs are otherwise anticipated.
- **Likely root cause:** Supported-type validation is performed on the full URL string rather than on the URL path's final segment.
- **Impact:** Signed, authenticated, cache-busted, and CDN download URLs that identify valid ROM assets in their path cannot be imported even though they are ordinary valid HTTPS URLs.
- **Repair direction:** Parse the URL or isolate the last path component before `?`/`#` for extension validation while preserving the original complete URL for the network request. Add tests for `.gb?token=...`, `.zip#fragment`, mixed-case extensions, and a negative case where `.gb` appears only in a query value.

- **Consolidation sources:** [automation/bug-scan-20260929-2102](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/728e209d547481d1060b7841b4e22c5eb837ff8c/bugs.md); [Drive: 2026-09-29 21-02 MDT - automation-bug-scan-20260929-2102 - Instructions](https://docs.google.com/spreadsheets/d/10o78oxZAN0DYrcpzIBXxrHkgQLsr9hocvSpV1IfPSEY/edit?usp=drivesdk); [Drive: 2026-09-29 21-02 MDT - automation-bug-scan-20260929-2102 - Diff](https://docs.google.com/spreadsheets/d/1llLFetNI6nEmi67v1zvyKkLU9xAapNH61elrKCuOF8s/edit?usp=drivesdk)

### 199. KOReader HTTP responses can grow an unbounded reallocating internal-heap buffer

- **Status:** Open.

- **Affected code:** `lib/KOReaderSync/KOReaderSyncClient.cpp`, the `ResponseBuffer` type, `httpEventHandler()`, `authenticate()`, and `getProgress()`.
- **Trigger / reproduction:** Configure KOReader Sync to use a custom/misbehaving server that returns a very large HTTP response body for `/users/auth` or `/syncs/progress/<document>` (for example several megabytes instead of the expected sub-kilobyte JSON), then perform authentication or progress sync.
- **Observed / logically demonstrated failure:** Every received data event calls `ResponseBuffer::ensure(buf->len + data_len + 1)`, which `realloc()`s the ordinary heap to the exact accumulated body size with no maximum response length. The client therefore keeps growing and repeatedly copying the response until heap pressure causes allocation failure. The event handler merely logs the failure and returns `ESP_OK`, allowing the transfer to continue while the already allocated prefix remains resident. The code comment that KOReader payloads are “tiny JSON (<1KB)” is not enforced anywhere.
- **Likely root cause:** The transport assumes a trusted small server response and has neither a protocol body-size limit nor a streaming/bounded parser.
- **Impact:** A malformed, compromised, or simply misconfigured custom sync endpoint can exhaust/fragment scarce ESP32 heap during an ordinary sync operation and destabilize unrelated TLS/UI work. Even after allocation failure the transfer continues consuming network/CPU resources rather than aborting promptly.
- **Repair direction:** Enforce a small explicit maximum response size appropriate to the KOReader schema, reject excessive `Content-Length` when available, stop the HTTP transfer as soon as the bound would be exceeded, and make allocation failure propagate as a hard request error. Prefer one bounded PSRAM-backed buffer or a streaming JSON parser instead of exact-size repeated reallocations. Add tests for a normal response, exactly-at-limit response, over-limit chunked response, and allocator failure.

- **Consolidation sources:** [automation/bug-scan-20260929-2120](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/82bef6c8270ad0ed072369ee19b07022dc82e181/bugs.md); [Drive: 2026-09-29 2120 MDT - automation-bug-scan-20260929-2120 - Instructions](https://docs.google.com/spreadsheets/d/1uI1Z04FNBkqdLQxhgZuCSZXUFQbOTUOFJ7dzYn39FoE/edit?usp=drivesdk); [Drive: 2026-09-29 2120 MDT - automation-bug-scan-20260929-2120 - Instructions](https://docs.google.com/document/d/1xv-O64ISDIO8zYQ7r6A9vc7x-RMMWrApoVqtPFzcB1U/edit?usp=drivesdk); [Drive: 2026-09-29 2120 MDT - automation-bug-scan-20260929-2120 - Diff](https://docs.google.com/spreadsheets/d/1v0enkcChAVBR3-RFLlrRcIS8MhmEQyj1AV0LlBx-aj4/edit?usp=drivesdk); [Drive: 2026-09-29 2120 MDT - automation-bug-scan-20260929-2120 - Diff](https://docs.google.com/spreadsheets/d/1Dq6s7qm3fIlKrMX_AVJaMlTG3V-Vfhf-Onk_qRqtUog/edit?usp=drivesdk); [Drive: 2026-09-29 2120 MDT - automation-bug-scan-20260929-2120 - Diff](https://docs.google.com/document/d/1Igvgxgg6gX8_I8-QVjW_Ve2n06-Dp58Adt6XIthpikI/edit?usp=drivesdk)

### 200. HTTPS downloads disable server certificate verification

- **Status:** Open.
- **Affected code:** `src/network/HttpDownloader.cpp::fetchUrl()`, `HttpDownloader::downloadToFile()`, and native HTTP streaming through `src/native/NativeStreamBridge.cpp::httpWorker()`.
- **Trigger / reproduction:** Use any HTTPS metadata or binary download that reaches the compatibility/worker `HTTPClient` path, then present a TLS endpoint whose certificate is expired, self-signed, for the wrong hostname, or issued by an untrusted CA. The native `open_http` worker reaches this same path because it runs on the dedicated HTTP task; `t5_app_get_api()` is owner-task scoped, so `invocationStreams()` returns null there.
- **Observed / logically demonstrated failure:** Both HTTPS branches allocate `NetworkClientSecure` and immediately call `setInsecure()`. The connection therefore skips peer-certificate and hostname authentication and can accept a server that is not the requested HTTPS origin.
- **Likely root cause:** TLS was configured for transport encryption only, without a trust anchor or certificate-bundle verification policy.
- **Impact:** A network attacker able to intercept traffic can impersonate catalog, manifest, OPDS, font, or other HTTPS endpoints and alter metadata/content before higher-level checks. Flows without their own content signature/digest are directly exposed, and HTTPS no longer supplies server authenticity.
- **Repair direction:** Remove `setInsecure()`; configure the project trust roots / certificate bundle for all HTTPS clients, retain hostname verification, and make certificate failures terminal. Add tests using trusted, self-signed, expired, and hostname-mismatched endpoints, including the native `open_http` worker path.

- **Consolidation sources:** [automation/bug-scan-20260929-2326](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/95f741bc0a49f92b0cec24836821ba216f48eb5a/bugs.md); [Drive: 2026-09-29 2326 MDT - automation-bug-scan-20260929-2326 - Diff](https://docs.google.com/spreadsheets/d/1AYPw1N2ZOYgHFucbaezdGbIxQMX-0HhylovtsgBIhJc/edit?usp=drivesdk); [Drive: 2026-09-29 2326 MDT - automation-bug-scan-20260929-2326 - Instructions](https://docs.google.com/spreadsheets/d/1DqYP7D3EugBNzyKRCNek4bVnZX3yy0vbmJllB1R6DBE/edit?usp=drivesdk)

### 201. Compatibility HTTP downloads report success even when the destination file fails to close

- **Status:** Open.
- **Affected code:** `src/network/HttpDownloader.cpp::HttpDownloader::downloadToFile()`, compatibility `HTTPClient` / `FileWriteStream` path.
- **Trigger / reproduction:** Complete an HTTP body with all writes returning the requested byte counts, then inject an SD/filesystem finalization failure so `FsFile::close()` returns false (for example media removal or a flush/commit failure at close).
- **Observed / logically demonstrated failure:** After `http.writeToStream(&fileStream)`, the function calls `file.close();` and discards its boolean result. If the streamed byte count matches Content-Length and `FileWriteStream::ok()` remains true, the function returns `HttpDownloader::OK` despite the failed finalization. The native staged stream path explicitly requires successful `finish()`, so the two transports have inconsistent durability semantics.
- **Likely root cause:** Transfer/write success is treated as equivalent to durable file publication; close-time filesystem errors are not part of the compatibility path's success predicate.
- **Impact:** Callers can validate, rename, index, or otherwise publish a file whose final filesystem commit failed, producing truncated/corrupt downloads while the UI reports success. This applies even when no previous destination existed, so it is distinct from overwrite-before-download loss.
- **Repair direction:** Treat `file.close() == false` as `FILE_ERROR`, remove only the file owned by the failed transfer, and for durable/staged callers reopen and verify expected length before publication. Add fault-injection coverage for close failure after a full-length write.

- **Consolidation sources:** [automation/bug-scan-20260929-2326](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/95f741bc0a49f92b0cec24836821ba216f48eb5a/bugs.md); [Drive: 2026-09-29 2326 MDT - automation-bug-scan-20260929-2326 - Diff](https://docs.google.com/spreadsheets/d/1AYPw1N2ZOYgHFucbaezdGbIxQMX-0HhylovtsgBIhJc/edit?usp=drivesdk); [Drive: 2026-09-29 2326 MDT - automation-bug-scan-20260929-2326 - Instructions](https://docs.google.com/spreadsheets/d/1DqYP7D3EugBNzyKRCNek4bVnZX3yy0vbmJllB1R6DBE/edit?usp=drivesdk)

### 202. reader.typography reads beyond the caller-declared text buffer and rejects valid length-bounded input

- **Status:** Open.

- **Affected code:** `src/native/NativeReaderTypography.cpp::nativeReaderPage()`; ABI contract in `sdk/driver/RiscReaderTypographyV1.h`; bounded UTF-8/layout helpers in `lib/GfxRenderer/ReaderPageLayout.h`.
- **Trigger / reproduction:** Call the installed `reader.typography` capability with a valid UTF-8 body stored in an exactly `length`-byte buffer that has no trailing NUL byte. This is valid under `risc_reader_page_request_v1`, which supplies `text` and its explicit byte `length` separately. A guard-page or heap-boundary allocation makes the over-read deterministic; otherwise place any nonzero byte immediately after the declared range.
- **Observed / logically demonstrated failure:** Before invoking the already length-bounded UTF-8 validator and page layout, `nativeReaderPage()` evaluates `strnlen(q->text, q->length + 1) != q->length`. That call probes byte `q->text[q->length]` when the declared body contains no embedded NUL. A valid non-NUL-terminated body is therefore accepted or rejected based on memory outside the ABI-declared range, and an exactly bounded buffer at an inaccessible boundary can fault on the one-byte over-read. The later body handling does not need this terminator: font preparation copies bounded chunks into local NUL-terminated buffers, and `readerPageLayout()` takes the explicit `length`.
- **Likely root cause:** A C-string validation check was added to an interface whose body is explicitly length-delimited. The check conflates “no embedded NUL inside the declared body” with “a NUL must exist one byte beyond it.”
- **Impact:** Third-party reader clients that correctly provide a length-bounded byte span can fail nondeterministically or crash the host depending on adjacent memory, violating the capability ABI and making otherwise valid text impossible to render safely.
- **Repair direction:** Remove the `strnlen(..., length + 1)` requirement for `q->text`. Rely on `readerUtf8Valid(q->text, q->length)`, which already rejects embedded NULs while staying inside the supplied range, and keep all downstream body processing length-bounded. Add host tests using a non-NUL-terminated exact-length buffer, a guard byte after the range, an embedded NUL, and a guard-page/end-of-allocation case.

- **Consolidation sources:** [automation/bug-scan-20260930-0022](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/2b932092e31b6d6b85bd45c7ada99cb728994e26/bugs.md); [Drive: 2026-09-30 0022 MDT - automation-bug-scan-20260930-0022 - Diff](https://docs.google.com/spreadsheets/d/1VGiMSPAY900zqoow0Q_kZG_D-96pFkos_O65zZAw_mo/edit?usp=drivesdk); [Drive: 2026-09-30 0022 MDT - automation-bug-scan-20260930-0022 - Instructions](https://docs.google.com/spreadsheets/d/1hniZfzD9J_jLioHAEqaS74cAsknU2Ri9qm5BXSs-J9Y/edit?usp=drivesdk)

### 203. EPUB PNG cover conversion ignores alpha and renders transparent pixels as visible ink

- **Status:** Open.

- **Affected code:** `lib/PngToBmpConverter/PngToBmpConverter.cpp`, `convertScanlineToGray()` and PNG chunk scanning in `pngFileToBmpStreamInternal()`; cover/thumbnail generation in `lib/Epub/Epub.cpp`.
- **Trigger / reproduction:** Use a valid EPUB cover PNG containing RGBA or grayscale+alpha pixels with alpha 0, for example a transparent black background around opaque artwork, then let the EPUB metadata path generate its cached cover/thumbnail BMP.
- **Observed / logically demonstrated failure:** The grayscale+alpha branch copies only the grayscale sample and discards alpha. The RGBA branch computes luminance from RGB and likewise discards alpha. The chunk scanner also skips transparency metadata rather than compositing it. Thus a fully transparent black pixel becomes black ink in the generated BMP instead of the white page/background; transparent edges and backgrounds can become dark blocks or halos. The separate framebuffer PNG path already composites alpha, so the two PNG render paths disagree on the same valid image.
- **Likely root cause:** The BMP conversion path was written as an RGB/luma conversion and never incorporated alpha compositing semantics.
- **Impact:** Standards-compliant transparent PNG covers can produce visibly corrupted cached covers and thumbnails, including large black regions that were transparent in the source.
- **Repair direction:** Composite alpha against the intended white e-paper background before dithering/luma output for RGBA and grayscale+alpha, and honor applicable `tRNS` transparency for palette/grayscale/RGB images. Add fixtures with fully transparent black, half-transparent colored edges, and indexed `tRNS`, and compare cover/thumbnail output with the framebuffer decoder's white-background semantics.

- **Consolidation sources:** [automation/bug-scan-20260930-0247-final](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/65f8b5d9fb1195953a68ccec63882f5533f5a07f/bugs.md); [Drive: 2026-09-30 02-47 MDT - automation-bug-scan-20260930-0247-final - Diff](https://docs.google.com/spreadsheets/d/1oeF8rknOMfAoXC_W8xChkdJbdheYV1kaZNt4k9dyrsM/edit?usp=drivesdk); [Drive: 2026-09-30 02-47 MDT - automation-bug-scan-20260930-0247-final - Instructions](https://docs.google.com/spreadsheets/d/1ASSpLAik3zeGb_FhhyutJXUKiqxVgAixSFrSd6DZq5E/edit?usp=drivesdk)

### 204. Driver Manager destroys the last good catalog before a refresh succeeds

- **Status:** Open.

- **Affected code:** `src/native/NativeDriverManagerBridge.cpp`, `catalogRefresh()`, with the canonical/aggregate/legacy catalog loaders.
- **Trigger / reproduction:** Successfully load a Driver Manager catalog, then trigger another catalog refresh while saved Wi-Fi is temporarily unavailable, the release index is unreachable/malformed, or all catalog fallbacks fail.
- **Observed / logically demonstrated failure:** `catalogRefresh()` calls `catalog.clear()` before `connectSavedWifi()`. If connection fails it returns `false` with the previously valid live catalog already erased. If canonical discovery fails, the function explicitly swaps the catalog with an empty vector before aggregate discovery and again before legacy discovery. The legacy loader also appends directly to the global vector, so a failed fallback can leave a partial catalog even though refresh reports failure.
- **Likely root cause:** Refresh mutates the process-global catalog while the replacement is still being acquired and validated instead of staging the candidate catalog transactionally.
- **Impact:** A transient network/catalog fault can turn a working Driver Manager session into **no drivers available** (or a partial legacy list) until a later refresh succeeds, discarding useful last-known-good discovery state even though the API reported that the refresh failed.
- **Repair direction:** Have every discovery path build a temporary vector and only swap it into `catalog` after the selected refresh path succeeds completely. On failure preserve the prior catalog unchanged. Add regressions for connect failure, malformed canonical/aggregate responses, and a legacy scan that accepts some candidates before a later rejection.

- **Consolidation sources:** [automation/bug-scan-20260930-0324](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/61f98d60fa4eda874405b00c72b9a576e4ca9924/bugs.md); [Drive: 2026-09-30 0324 MDT - automation_bug-scan-20260930-0324 - Diff](https://docs.google.com/spreadsheets/d/1353mBOSb02ehViW2gfxo_G4Bsw-FXIdsTVKBs4qvn7g/edit?usp=drivesdk); [Drive: 2026-09-30 0324 MDT - automation_bug-scan-20260930-0324 - Instructions](https://docs.google.com/spreadsheets/d/1rMBCmHgaBRqtd-LRVm6Fvx8mkcjYuqjssDPGuXNM0yk/edit?usp=drivesdk)

### 205. File Browser leaks an open USB file handle when a source file is larger than `size_t`

- **Status:** Open.

- **Affected code:** `Apps/file_browser.c`, `copy_usb_to_sd()`; USB storage ABI `file_open_read` / `file_close`.
- **Trigger / reproduction:** Expose a USB storage file whose reported 64-bit size exceeds `SIZE_MAX` and copy it from USB to SD. This is especially relevant on ESP32-S3 where `size_t` is 32-bit while the volume API deliberately reports file size through `uint64_t`.
- **Observed / logically demonstrated failure:** `copy_usb_to_sd()` first calls `file_open_read(..., &source_size)`. It then executes `if (!input || source_size > SIZE_MAX) return false;`. When the open succeeds but the size is too large, the function returns without calling `usb_volume->file_close()` on the non-null `input` handle. Other exits after a successful open do perform the close.
- **Likely root cause:** The oversize guard is placed after resource acquisition but before the function's cleanup path.
- **Impact:** Each attempted copy of an oversized USB file can leak a mass-storage file handle/provider resource. Repeated attempts can exhaust handles, retain volume state, or cause later USB file operations to fail until the app/provider is restarted.
- **Repair direction:** Close every successfully acquired input handle on all exits, preferably through one cleanup path/RAII-style wrapper. Either reject >`SIZE_MAX` sources only after guaranteed cleanup or implement a 64-bit remaining-byte copy loop where the storage sink permits it. Add a mock-volume regression returning a valid handle plus `source_size > SIZE_MAX` and assert exactly one matching close occurs.

- **Consolidation sources:** [automation/bug-scan-20260930-0324](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/61f98d60fa4eda874405b00c72b9a576e4ca9924/bugs.md); [Drive: 2026-09-30 0324 MDT - automation_bug-scan-20260930-0324 - Diff](https://docs.google.com/spreadsheets/d/1353mBOSb02ehViW2gfxo_G4Bsw-FXIdsTVKBs4qvn7g/edit?usp=drivesdk); [Drive: 2026-09-30 0324 MDT - automation_bug-scan-20260930-0324 - Instructions](https://docs.google.com/spreadsheets/d/1rMBCmHgaBRqtd-LRVm6Fvx8mkcjYuqjssDPGuXNM0yk/edit?usp=drivesdk)

### 206. Wi-Fi Settings cannot choose another network while the saved last network remains reachable

- **Status:** Open.
- **Affected code:** `Apps/wifi_settings.c`, the **Choose another network** action; `src/native/NativeSystemUiBridge.cpp`, `wifiRequest()` / `NativeWifiActivity::onEnter()`; `src/activities/network/WifiSelectionActivity.h` and `.cpp`, the default `autoConnect` constructor argument and `onEnter()` / `checkConnectionStatus()`.
- **Trigger / reproduction:** Save valid credentials for network A and leave A reachable as the last-connected network. Open **Wi-Fi Networks**, choose **Choose another network**, while a different network B is also available.
- **Observed / logically demonstrated failure:** The app calls `system_ui->wifi_request()`. `NativeWifiActivity` constructs `WifiSelectionActivity` without overriding its default `autoConnect = true`. On entry, the selector finds the saved last SSID, calls `attemptConnection()`, and returns before starting a scan. While `AUTO_CONNECTING`, the loop only checks connection status. When A reconnects successfully with the saved password, `checkConnectionStatus()` immediately calls `onComplete(true)`, so the selector closes without ever displaying the network list or accepting a choice of B. Repeating **Choose another network** repeats the same reconnection to A.
- **Likely root cause:** The same auto-connect-by-default selector is reused for an explicit network-selection workflow. The System UI request has no mode that tells the selector that the user's intent is to browse/switch rather than reconnect the preferred network.
- **Impact:** As long as the preferred saved network remains reachable, the advertised Wi-Fi management action cannot switch the device to another network. The user must first make A fail or remove its saved state through another path.
- **Repair direction:** Make the explicit **Choose another network** handoff construct `WifiSelectionActivity(..., false)`, or add an explicit request mode/flag distinguishing reconnect from browse/select. Preserve auto-connect only for workflows that want it. Add a regression with reachable saved A plus visible B proving the action renders the list and can select B.

- **Consolidation sources:** [automation/bug-scan-20260930-0419](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/8f3a068b9f2d6105e008255713ee5086691a134a/bugs.md); [Drive: 2026-09-30 0419 MDT - automation_bug-scan-20260930-0419 - Diff](https://docs.google.com/spreadsheets/d/1o6Bfq-I4ygMn_Ai6dZFXLxodBuN4p08HsGPgkuWaXGo/edit?usp=drivesdk); [Drive: 2026-09-30 0419 MDT - automation_bug-scan-20260930-0419 - Instructions](https://docs.google.com/spreadsheets/d/1PyHqC8xFRzQs02Mt1-ory7jR4LOiSeqVqJ1dFvpgQzg/edit?usp=drivesdk)

### 207. EPUB relative internal links can jump to the wrong chapter when basenames are duplicated

- **Status:** Open.
- **Affected code:** `lib/Epub/Epub.cpp::resolveHrefToSpineIndex()`; caller `src/activities/reader/EpubReaderActivity.cpp::navigateToHref()`.
- **Trigger / reproduction:** Create a valid EPUB containing both `OPS/part1/note.xhtml` and `OPS/part2/note.xhtml`. From `OPS/part2/chapter.xhtml`, follow a relative link such as `note.xhtml#detail` intended for the sibling `OPS/part2/note.xhtml`.
- **Observed / logically demonstrated failure:** The resolver strips the fragment and first compares the unresolved string `note.xhtml` directly against full spine hrefs. When that fails, it falls back to comparing only basenames and returns the first spine entry named `note.xhtml`. If `part1/note.xhtml` appears first, the reader navigates to the wrong document even though the link is valid and unambiguous relative to its source chapter.
- **Likely root cause:** Relative hrefs are not resolved against the current document's containing directory before spine lookup; basename-only matching is used as an ambiguity-blind fallback.
- **Impact:** Footnotes, cross-references, and ordinary internal links in valid EPUBs with repeated filenames across directories can navigate to unrelated content.
- **Repair direction:** Resolve the link path against the current spine item's directory, normalize dot segments, remove only the fragment for lookup, and perform an exact canonical-path match. If a basename compatibility fallback is retained, allow it only when the basename is unique. Add fixtures for duplicate basenames, sibling links, and `../` references.

- **Consolidation sources:** [automation/bug-scan-20260930-0518](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/82533918d81fda11ae9c6b19d95b071a98f19361/bugs.md); [Drive: 2026-09-30 0518 MDT - automation-bug-scan-20260930-0518 - Diff](https://docs.google.com/spreadsheets/d/1geT64642Lsjo_EaRZhNxF9516LV2BRVIp4stNAnOkjg/edit?usp=drivesdk); [Drive: 2026-09-30 0518 MDT - automation-bug-scan-20260930-0518 - Instructions](https://docs.google.com/spreadsheets/d/1xB4JDjKoHfw-mCY9pUQL1VP2tWxilaT0WRwdCPryrO8/edit?usp=drivesdk)


### 208. CrossPoint state loading accepts schema-invalid JSON as a successful empty state

- **Status:** Open.
- **Affected code:** `src/JsonSettingsIO.cpp::loadState()`; `src/CrossPointState.cpp::loadFromFile()`; state persistence through `JsonSettingsIO::saveState()`.
- **Trigger / reproduction:** Put a non-empty, syntactically valid but schema-invalid `/.crosspoint/state.json` on the SD card, for example `{}`, `[]`, or an object missing the normal state fields. A legacy `/.crosspoint/state.bin` may still contain recoverable state.
- **Observed / logically demonstrated failure:** `loadState()` checks only whether `deserializeJson()` succeeds. It never requires a JSON object or validates the expected field types/presence, then assigns defaults for absent values, clears `recentSleepImages`, and returns `true`. Because `CrossPointState::loadFromFile()` immediately returns that result for any non-empty JSON file, a schema-invalid JSON document is treated as authoritative success and the binary fallback is never attempted. The live `openEpubPath`, sleep-image history, reader-load guard, and `lastSleepFromReader` are consequently reset; the next successful state save can persist those defaults over the malformed JSON.
- **Likely root cause:** State deserialization has parse-syntax validation but no document/schema validation, while the caller interprets `true` as “usable state loaded” and suppresses recovery.
- **Impact:** A syntactically valid but structurally wrong state file can silently discard recoverable reader/sleep state and defeat the existing binary migration fallback instead of entering an explicit recovery path.
- **Repair direction:** Require an object root and validate a minimum coherent state schema before mutating `CrossPointState`. Parse into a temporary state object, reject wrong field types/impossible ring metadata, and commit only after validation succeeds. On schema failure let `loadFromFile()` continue to an available legacy/recovery source rather than reporting success. Add regressions for `{}`, `[]`, wrong-typed fields, and a valid legacy fallback.

### 209. SD firmware validation accepts ESP images whose segment count exceeds the bootloader limit

- **Status:** Open.
- **Affected code:** `src/network/FirmwareFlasher.cpp::validateImageFile()` and `flashFromSdPath()`.
- **Trigger / reproduction:** Provide an ESP application image with the correct 0xE9 magic, `segment_count = 17`, seventeen structurally in-bounds segment records, a matching image checksum, matching optional appended SHA-256, the expected board marker, and a total size that fits the OTA partition.
- **Observed / logically demonstrated failure:** `validateImageFile()` copies header byte 1 into `segCount` and iterates all declared segments, but never enforces ESP-IDF's `ESP_IMAGE_MAX_SEGMENTS` limit of 16. A self-consistent 17-segment image can therefore pass every check in this validator and return `OK`. `flashFromSdPath()` then writes it to the next OTA partition and can select that partition even though the ESP bootloader's image validator rejects a header whose segment count exceeds 16.
- **Likely root cause:** The custom SD validator reproduces checksum/hash/bounds checks from the ESP image format but omits the bootloader's segment-count invariant.
- **Impact:** Firmware Flasher can label an image valid, erase/program the OTA slot, and switch boot selection to an image the bootloader will not accept. The update then fails only at reboot/rollback time instead of being rejected safely before flash mutation.
- **Repair direction:** Reject `segCount > ESP_IMAGE_MAX_SEGMENTS` before iterating segments (using the ESP-IDF constant/header rather than a duplicate magic number where practical), and keep the validator aligned with bootloader header validation. Add boundary fixtures proving 16 segments can pass and 17 is rejected before any erase/write.

### 210. A stale RTC variant hint overrides successful hardware probing and can select the wrong register map

- **Status:** Open.
- **Affected code:** `lib/hal/HalClock.cpp::begin()`, `configure()`, `timeStartRegister()`, `syncSystemTimeFromRtc()`, and `syncRtcFromSystemTime()`; startup configuration in `src/main.cpp`; persisted `CrossPointSettings::rtcVariantHint`.
- **Trigger / reproduction:** Boot hardware containing a PCF8563 with settings carrying `rtcVariantHint = 1` (PCF85063), for example after cloning/moving an SD settings file from a board with the other RTC variant or after the persisted hint becomes stale. The reverse mismatch is analogous.
- **Observed / logically demonstrated failure:** `begin()` probes both layouts and can correctly select PCF8563, but `configure()` then unconditionally replaces `variant_` with any nonzero persisted hint whenever an RTC is merely `available_`; it does not verify that the hinted layout probed successfully. Subsequent reads therefore start at register 0x04 instead of the PCF8563 time base 0x02. A clock write using the wrong PCF85063 layout writes seven bytes beginning at 0x04, which on PCF8563 covers Hours through Years and then Minute_alarm/Hour_alarm rather than Seconds through Years, so a stale hint can both misread time and overwrite unrelated alarm registers.
- **Likely root cause:** The persisted hint is treated as authoritative identity rather than as a tie-breaker/cache constrained by the hardware probes performed moments earlier.
- **Impact:** Reusing settings across RTC variants, replacing hardware, or retaining a stale hint can make boot-time clock recovery fail or produce nonsensical time and can corrupt RTC alarm state during synchronization.
- **Repair direction:** Retain the successful-probe set from `begin()` and honor a persisted hint only if that layout was positively detected; if exactly one layout probes, it must win. Use the hint only to disambiguate genuinely ambiguous probe results, and avoid write-based “verification” against a layout that did not probe. Add PCF85063/PCF8563 register-map fakes with deliberately contradictory hints and assert no wrong-layout write occurs.
