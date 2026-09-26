# Bug Log

## 2026-09-26 scan

### 1. Battery Status drops the minus sign for temperatures from -0.1 C through -0.9 C

- **Status:** Incomplete — fix implemented and focused regression-tested on branch `fix/battery-negative-subdegree-temperature`; PR creation is still pending. Resume this same branch and open a single PR targeting `master` rather than creating a new branch.
- **Affected code:** `Apps/battery.c`, `add_temperature(uint16_t deci_kelvin)`.
- **Trigger / reproduction:** Supply battery telemetry where `temperature_dk` converts to a deci-Celsius value between `-1` and `-9` (for example, `2730` deci-kelvin produces `deci_c == -1`). Open Battery Status and inspect the Temperature row.
- **Observed / logically demonstrated failure:** The formatter computes `deci_c / 10` and a separately absolute-valued fractional digit. In C integer division truncates toward zero, so `-1 / 10` is `0`; the screen therefore renders `0.1 C` instead of `-0.1 C`. The same sign loss occurs through `-0.9 C`.
- **Likely root cause:** Sign handling is split across the integer and fractional pieces, but the integer piece becomes zero for magnitudes below 1 degree.
- **Impact:** Cold battery telemetry near freezing is displayed with the wrong sign, which can mislead diagnostics and temperature-related charging decisions.
- **Repair direction:** Format the sign independently from the magnitude (for example, convert to an absolute deci-C magnitude after recording `deci_c < 0`) and render `"%s%d.%d C"`. Add regression cases for `-0.1 C`, `-0.9 C`, `-1.0 C`, `0.0 C`, and positive values.

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
- **Observed / logically demonstrated failure:** The chrome labels `previous_label = "Up"` and `next_label = "Down"`, but `T5_UI_EVENT_PREVIOUS` increments `scroll` while `T5_UI_EVENT_NEXT` decrements it. With `scroll == 0`, Up advances deeper into the document and Down cannot move at all; after scrolling, Down moves toward the top.
- **Likely root cause:** The scroll-delta conditions were inverted when the title detail view was added.
- **Impact:** Physical/button navigation contradicts the UI labels and the rest of the application navigation model, making long title details awkward to read.
- **Repair direction:** On **Up/PREVIOUS**, decrement `scroll` when `scroll > 0`; on **Down/NEXT**, increment `scroll` when `scroll < result.max_scroll_lines`. Add a focused test that checks both boundary conditions and direction.

## Duplicate check performed

At scan time, `bugs.md` did not yet exist on `master`, the repository has GitHub Issues disabled/no issue records returned, and the current open PRs were reviewed for overlap. PR #198 concerns GT911 touch capture, PR #194 concerns global Home shortcuts, and PR #96 is the U1 implementation branch. Closed PR #183 introduced the Rom Manager title-detail view but does not document the reversed Up/Down behavior above.
