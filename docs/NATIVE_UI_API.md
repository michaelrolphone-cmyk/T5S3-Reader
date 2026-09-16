# RiscRTE UI Host API

## Target platform contract

RiscRTE applications describe presentation and navigation through framework-owned UI capabilities rather than reproducing board-specific rendering, spacing, input mapping, or navigation behavior. UI is a platform service under the master specification in `RISCRTE_PLATFORM_SPEC.md` and the application/scene model in `SCENE_RUNTIME_ARCHITECTURE.md`.

The target contract is device-independent: applications own content, state and actions; RiscRTE owns presentation policy, active theme, input mapping, standard navigation semantics and system chrome. Future evolution should expose these facilities through RiscRTE capability/service terminology. New application code must not introduce a second private UI framework when the platform service can express the screen.

## Current compatibility implementation

The deployed ABI is named `T5UiApi`. The `T5*` identifier is retained here because it is a real versioned compatibility symbol/header and must not be renamed casually. It does not identify the platform, whose name is RiscRTE.

`T5UiApi` lets SD-installed ELF applications use RiscRTE firmware presentation and navigation behavior instead of duplicating it in application code. The public compatibility header is `lib/NativeApps/include/T5UiApi.h`; callers request version 1 through `t5_ui_get_api(T5_UI_API_VERSION)` and verify `struct_size` through the last required member. Firmware 1.1.8 is the first release that exports this entry point.

### Ownership boundary

RiscRTE currently owns active `UITheme` metrics/rendering, firmware UI fonts, standard headers/subtitles, `GUI.drawList`, firmware table rendering, wrapped user-content text, status placement, `GUI.drawButtonHints`, front-button mapping, touch hit-testing, orientation correction, `ButtonNavigator` previous/next behavior, Confirm-on-release, Back handling, and Power/Home native-session exit behavior.

The application owns application data, persistence, business logic, screen state, selection/scroll state, and actions. This is the compatibility implementation of the broader RiscRTE rule that applications describe what should be presented rather than encode hardware-specific presentation mechanics.

### Current surfaces

`t5_ui_chrome_t` supplies logical title, subtitle, status and button labels. List rows supply title/subtitle/value data and are rendered using the active firmware theme. Tables use weighted columns instead of device pixel coordinates. `render_text_view()` supplies a wrapped document/chat/log viewport using the firmware's user-content font policy and returns line/scroll metadata.

`poll_event()` is the standard compatibility navigation path. It returns semantic PREVIOUS, NEXT, CONFIRM, BACK, TAP and EXIT events rather than requiring applications to interpret raw physical buttons. `hit_test()` applies to the most recently rendered list or table. Physical button remapping remains firmware-owned.

### Reference applications

`Apps/timecard.c` is the reference structured-data application: week history/day editing use firmware lists and the weekly punch summary uses the framework table. `Apps/llm_ask.c` is the reference text/network application: its transcript uses the framework text viewport while the ELF owns provider protocol, transient state and application behavior.

## Migration rule

Keep `T5UiApi`, `t5_ui_get_api`, `T5_UI_*`, current source paths and firmware class names in documentation whenever they identify deployed code. New specifications and conceptual discussion use **RiscRTE UI Host API**, **RiscRTE UI service**, application, scene, capability and framework terminology. An eventual ABI rename requires an explicit compatibility/migration plan; documentation alone must not break existing ELF applications.