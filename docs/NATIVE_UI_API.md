# Native firmware UI service

`T5UiApi` lets SD-installed native ELF applications use the firmware's existing UI presentation and navigation behavior instead of duplicating it in application code.

The public header is `lib/NativeApps/include/T5UiApi.h`. Request version 1 with:

```c
#include "T5UiApi.h"

const t5_ui_api_v1 *ui = t5_ui_get_api(T5_UI_API_VERSION);
if (!ui) return;
```

As with the other native services, callers should verify `struct_size` through the last member they require. Firmware 1.1.8 is the first release that exports `t5_ui_get_api`.

## Responsibilities

The firmware owns:

- active `UITheme` metrics and rendering;
- firmware UI fonts;
- standard headers and subtitles;
- `GUI.drawList` list rendering and selection behavior;
- firmware-owned table rendering for structured app data;
- wrapped user-content text rendering;
- status-line placement;
- `GUI.drawButtonHints`;
- front-button label mapping from the user's Controls settings;
- touch hit-testing for the mapped bottom button hints;
- orientation correction for touch button bounds;
- `ButtonNavigator` previous/next semantics, including Up/Left, Down/Right, and hold-repeat;
- Confirm-on-release and Back handling;
- Power/Home native-session exit behavior.

The ELF continues to own its application data, persistence, business logic, screen state, selected index, scrolling state, and actions.

This split is intentional. Applications describe the content they want displayed; they should not reproduce firmware spacing, fonts, button positions, physical-button mapping, or user-content font handling.

## Chrome

List, table, and text-view screens use `t5_ui_chrome_t`:

```c
t5_ui_chrome_t chrome = {
    .title = "Time Card",
    .subtitle = "Weeks",
    .status = "Select a week",
    .back_label = "Home",
    .confirm_label = "Open",
    .previous_label = "Up",
    .next_label = "Down",
};
```

The firmware maps these logical labels onto the current physical front-button configuration before drawing the standard button hints.

## Lists

A list row can supply a title, optional subtitle, and optional right-side value:

```c
t5_ui_list_row_t rows[] = {
    {
        .title = "Clock in",
        .value = "8:00 AM",
        .flags = T5_UI_LIST_HIGHLIGHT_VALUE,
    },
};

ui->render_list(&chrome, rows, 1, selected_index);
```

The bridge uses the current firmware theme's standard list renderer. Pagination, selection inversion, row typography, truncation, and page indicators remain firmware-owned.

## Tables

Tables use weighted columns so an application does not hard-code device pixel coordinates:

```c
static const t5_ui_table_column_t columns[] = {
    {"Day", 15},
    {"In", 21},
    {"Start", 21},
    {"End", 21},
    {"Out", 22},
};

t5_ui_table_row_t rows[11] = {0};
rows[0].cells[0] = "Sun 13";
rows[0].cells[1] = "8:00 AM";
rows[0].cells[2] = "12:00 PM";
rows[0].cells[3] = "12:30 PM";
rows[0].cells[4] = "4:30 PM";

ui->render_table(&chrome, columns, 5, rows, 11, selected_index);
```

A row with `T5_UI_TABLE_ROW_FULL_WIDTH` uses its first cell as a full-width action row. Timecard uses these rows for Clock in, Lunch start, Lunch end, and Clock out below the seven day rows.

## Wrapped text viewport

`render_text_view()` is for document/chat/log style native-app content that should use the same user-content font policy and layout as the firmware:

```c
t5_ui_text_view_result_t layout = {0};
ui->render_text_view(&chrome, transcript, scroll_from_bottom, &layout);
```

The firmware:

- uses the active theme's content bounds and system chrome;
- renders body text as `TextRole::UserContent`;
- wraps text with the firmware's selected user-content font;
- preserves newline paragraph and blank-line breaks;
- reports total, visible, and maximum scrollable line counts.

`scroll_from_bottom == 0` shows the newest/bottom page. Increasing it moves toward older content. The ELF retains the scroll value and decides which input events alter it.

`Apps/llm_ask.c` uses this viewport for its conversation transcript, so the Ask app no longer needs a private rendering implementation even though the LLM service itself is fully app-owned.

## Navigation

Use `poll_event()` rather than interpreting raw button bits when the app wants standard firmware navigation:

```c
t5_ui_event_t event;
while (ui->poll_event(&event, 20)) {
    switch (event.type) {
        case T5_UI_EVENT_PREVIOUS:
            selected = ui->previous_index(selected, item_count);
            break;
        case T5_UI_EVENT_NEXT:
            selected = ui->next_index(selected, item_count);
            break;
        case T5_UI_EVENT_CONFIRM:
            activate_selected();
            break;
        case T5_UI_EVENT_BACK:
            go_back();
            break;
        case T5_UI_EVENT_TAP: {
            const int32_t hit = ui->hit_test(event.touch_x, event.touch_y);
            if (hit >= 0) {
                selected = hit;
                activate_selected();
            }
            break;
        }
        case T5_UI_EVENT_EXIT:
            return;
        default:
            break;
    }
}
```

`PREVIOUS` and `NEXT` are generated through the same `ButtonNavigator` used by firmware activities. They therefore follow the normal Up/Left and Down/Right aliases and continuous-navigation timing. Bottom button-hint taps are translated through `MappedInputManager::resolveTouchFrontButton`, so remapped physical buttons remain consistent with firmware screens.

`hit_test()` applies to the most recently rendered list or table. It returns a row index, `T5_UI_HIT_HEADER`, or `T5_UI_HIT_NONE`.

## Reference applications

`Apps/timecard.c` is the reference structured-data application. Its week history and day editor are firmware lists, and its weekly punch summary is a firmware-owned table.

`Apps/llm_ask.c` is the reference text/network application. Its conversation transcript uses the firmware text viewport and navigation controls while the ELF owns the LLM provider, HTTP protocol, transient chat state, and application behavior.
