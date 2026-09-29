# Bug Log

Consolidated on 2026-09-27 from scheduled scan/fix branches and their Google Drive handoffs, against master `6d876ae443d873d06068ae866fa4eda403b95f90`. This is the canonical bug list. IDs are stable; repeated scan-local numbers 13–15 have been replaced with unique IDs. Entries retain the original reproduction evidence and repair direction; these are source-based reports, not claims of hardware reproduction.

**64 unresolved distinct reports after merging this branch.** Eight scheduled fixes are included in [PR #244](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/244) and removed from the active list here; they remain open on master until merge. Their original reports and source branches remain in the parent version of this file and in the PR description. Fixes already merged through PRs [#205](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/205) (battery temperature), [#209](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/209) (Wi-Fi redraw), and [#212](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/212) (Rom Manager scrolling) are excluded from the active list.

## Coverage and recovery

- Inspected all 28 `automation/bug-scan-*` branches and all eight pending scheduled fix branches through the 2026-09-27 15:24 MDT scan. Read 92 handoff files in the Bug Scan Diffs/Instructions and Bug Fix Work folders, including native Docs/Sheets and raw files.
- Recovered uncommitted reports from the 2026-09-26 17:30, 18:23, 19:21 and 2026-09-27 06:24, 10:22, 12:21 Drive handoffs.
- Merged repeated App Store/Package Manager row limits, Button Remap rollback, SD Firmware handoff, destination-picker overflow, native read truncation, legacy string deserialization, external-volume hiding and LoRa restoration findings. Distinct persistence/load failures remain separate and retain their affected functions.
- The 2026-09-26 14:21, 15:21 and 17:24 branches contain no changes beyond their master bases. No populated handoff was found for those runs; the two 17:24 documents are empty. No findings were invented for empty runs. The 17:30 handoff's actual findings are included.
- Original branches and Drive files are retained as provenance. Old per-branch bug statuses are historical; this document and the consolidated PR supersede them.

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


### 133. Time Card keeps failed punch edits live and can persist them on a later successful save

- **Status:** Open.
- **Sources:** current master `8f85ae659a2e48619be8d0e678ea38778c49d2ba`; no current open issue or PR covers this failed-save state mutation.
- **Affected code:** `Apps/timecard.c`, especially `ensure_day()`, `set_punch()`, `punch_today()`, and `consume_keyboard()`.
- **Trigger / reproduction:** Start with a valid Time Card store, then force `storage->write_file_atomic()` to fail for one clock punch or manual edit. Leave the app running, allow storage writes to recover, and make a later punch/edit that saves successfully. Also exercise the same failure while `day_count == MAX_DAYS` and the failed operation creates a new date.
- **Observed / logically demonstrated failure:** `set_punch()` mutates the live `days[]` model before calling `save_store()` and does not restore that mutation when persistence fails. `punch_today()` reports **Could not save punch**, and the manual-edit path reports failure, but the changed punch remains in memory. A later successful `save_store()` serializes that previously failed change along with the new one. At the 400-day limit, `ensure_day()` can also evict the oldest in-memory day before the failed save, and that eviction can become durable on the next successful write.
- **Likely root cause:** The Time Card write path uses mutate-then-persist semantics without a snapshot, staged model, or rollback on persistence failure.
- **Impact:** A punch/edit that the UI explicitly reports as unsaved can later become durable without the user's knowledge. At capacity, a failed operation can also prime an older valid day for later deletion.
- **Repair direction:** Make punch updates transactional. Stage the candidate day array/count (including any capacity eviction), persist the staged representation, and publish it to the live model only after the write succeeds; alternatively snapshot and restore every affected element/count on failure. Add fault-injection tests proving a failed existing-day edit, failed new-day insertion, and failed insertion at `MAX_DAYS` leave both live and durable history unchanged after subsequent successful saves.

### 134. KOReader document matching toggles repeatedly from one held Confirm press

- **Status:** Open.
- **Sources:** current master `8f85ae659a2e48619be8d0e678ea38778c49d2ba`; no current open issue or PR covers KOReader input edge handling.
- **Affected code:** `Apps/koreader_sync.c`, `activate_selected()` and the main `app_main()` input loop; the level-triggered button contract exported by `src/native/NativeAppHost.cpp::poll()`.
- **Trigger / reproduction:** Open **KOReader Sync**, select **Document Matching**, then press and hold Confirm for longer than one 50 ms polling interval.
- **Observed / logically demonstrated failure:** The app tests `input.buttons & T5_APP_BUTTON_CONFIRM` on every raw app poll and calls `activate_selected()` each time the bit remains asserted. For the Document Matching row, every call flips Filename/Binary and persists the new value. One physical hold can therefore toggle and write the setting several times, with the final mode determined by release timing rather than one deliberate activation.
- **Likely root cause:** KOReader Sync treats the native app ABI's level-triggered button state as a one-shot event and does not gate Confirm on a rising edge or release.
- **Impact:** A user can release Confirm with the opposite matching mode from the one they intended, while one press causes multiple unnecessary settings writes.
- **Repair direction:** Consume edge-based UI events for this screen or track the previous button mask and activate only on the Confirm rising edge. Add a regression that keeps Confirm asserted across several polls and verifies exactly one match-method change and one persistence attempt.

### 135. Rom Manager silently truncates Vimm browse/search results after 96 entries

- **Status:** Open.
- **Sources:** current master `8f85ae659a2e48619be8d0e678ea38778c49d2ba`; no current open issue or PR covers this Vimm result-window limit.
- **Affected code:** `Apps/rom_manager.c`, `MAX_VIMM`, `fetch_vimm_url()`, `add_vimm_entry()`, `fetch_vimm()`, `search_vimm()`, and `open_vimm_page()`.
- **Trigger / reproduction:** Load a Vimm browse, letter, or search response containing more than 96 valid navigation/game entries. On the top-level browse response, page/navigation links and games share the same 96-entry array, so enough navigation entries reduce the number of game rows that can be retained even further.
- **Observed / logically demonstrated failure:** Both parsing loops in `fetch_vimm_url()` stop when `vimm_count == MAX_VIMM`. The function still returns success, exposes only the retained prefix, and provides no continuation cursor, next page, or truncation indication. Because navigation entries are added before game rows when `include_pages` is true, they consume the same fixed capacity and can make valid games disappear from the very response that successfully loaded them.
- **Likely root cause:** A fixed render/result buffer is also being used as the complete remote catalog model, with no pagination state or overflow contract.
- **Impact:** Valid Game Boy catalog titles beyond the retained prefix cannot be discovered or opened/installed through Rom Manager, and the visible result set can vary with unrelated navigation-link count.
- **Repair direction:** Separate navigation from title storage and page/stream remote results instead of treating `MAX_VIMM` as the catalog size. Preserve a continuation/page cursor or virtualize rows over parsed results, and visibly report truncation if the remote source cannot be paged. Add tests with 96, 97, and substantially larger result sets, including a top-level response where navigation entries plus games exceed 96.
