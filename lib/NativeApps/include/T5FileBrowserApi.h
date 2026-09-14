#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_FILE_BROWSER_API_VERSION 1u
#define T5_FILE_BROWSER_NAME_MAX 128u

typedef struct {
    const char *name;        // Basename only; directories do not need a trailing slash.
    bool is_directory;
} t5_file_browser_entry_t;

typedef enum {
    T5_FILE_BROWSER_EVENT_NONE = 0,
    T5_FILE_BROWSER_EVENT_PREVIOUS = 1,
    T5_FILE_BROWSER_EVENT_NEXT = 2,
    T5_FILE_BROWSER_EVENT_PAGE_PREVIOUS = 3,
    T5_FILE_BROWSER_EVENT_PAGE_NEXT = 4,
    T5_FILE_BROWSER_EVENT_OPEN = 5,
    T5_FILE_BROWSER_EVENT_BACK = 6,
    T5_FILE_BROWSER_EVENT_ROOT = 7,
    T5_FILE_BROWSER_EVENT_DELETE = 8,
    T5_FILE_BROWSER_EVENT_ROW = 9,
    T5_FILE_BROWSER_EVENT_EXIT = 10,
} t5_file_browser_event_type_t;

typedef struct {
    uint8_t type;
    int32_t row_index;
} t5_file_browser_event_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    // Firmware settings used by the built-in FileBrowserActivity.
    bool (*show_hidden_files)(void);

    // Pixel-compatible browser renderer: active theme, fonts, file icons,
    // extension column, path truncation, button mapping and balanced refresh.
    void (*render)(const char *path,
                   const char *status,
                   const t5_file_browser_entry_t *entries,
                   uint32_t entry_count,
                   int32_t selected_index);

    // Input semantics matching FileBrowserActivity: release navigation,
    // continuous page jumps, one-second Back-to-root, one-second Confirm-to-delete,
    // touch rows/button hints and vertical page swipes.
    bool (*poll_event)(t5_file_browser_event_t *event,
                       uint32_t wait_ms,
                       bool at_root,
                       bool selected_is_directory);

    // Standard firmware delete confirmation. The requesting ELF is unloaded while
    // the firmware activity runs and is then relaunched; cookie is returned intact.
    bool (*confirm_delete_request)(const char *entry_name, uint64_t cookie);
    bool (*confirm_delete_take_result)(bool *confirmed, uint64_t *cookie);

    // Delete exactly as the firmware browser does, including EPUB cache cleanup.
    // Paths use the firmware SD namespace (for example "/Books/title.epub").
    bool (*delete_document)(const char *path);

    // Queue the standard firmware reader for a supported document. Caller returns
    // from app_main after success; the built-in browser remains untouched.
    bool (*open_document)(const char *path);

    // Run any loose .elf exactly as Browse Files does. The browser ELF is unloaded,
    // the child runs, and the browser is relaunched with a result for the cookie.
    bool (*launch_elf_request)(const char *sd_vfs_path, uint64_t cookie);
    bool (*launch_elf_take_result)(int32_t *esp_error, uint64_t *cookie);
} t5_file_browser_api_v1;

const t5_file_browser_api_v1 *t5_file_browser_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
