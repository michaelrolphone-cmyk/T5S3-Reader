"""Stage observation hooks into pinned IDF hub.c, preserving USB behavior."""


def instrument(source):
    def replace_once(old, new):
        nonlocal source
        if source.count(old) != 1:
            raise ValueError('Pinned IDF enumeration hook anchor changed: ' + old)
        source = source.replace(old, new, 1)

    # Keep the source cache pristine. These calls stay inside the controller
    # ELF; no firmware logging sink or persistent callback is installed.
    declarations = '''
extern void risc_usb_enum_reset(void);
extern void risc_usb_enum_stage(const char *stage);
extern void risc_usb_enum_error(const char *format, ...);
#define RISC_USB_ENUM_ERROR(format, ...) do { \\
    risc_usb_enum_error(format, ##__VA_ARGS__); \\
    ESP_LOGE(HUB_DRIVER_TAG, format, ##__VA_ARGS__); \\
} while (0)
'''
    replace_once('#include "sdkconfig.h"', '#include "sdkconfig.h"\n' + declarations)
    replace_once('case HCD_PORT_EVENT_CONNECTION: {',
                 'case HCD_PORT_EVENT_CONNECTION: {\n            risc_usb_enum_reset();')
    replace_once('enum_ctrl_t *enum_ctrl = &p_hub_driver_obj->single_thread.enum_ctrl;\n    switch (enum_ctrl->stage)',
                 'enum_ctrl_t *enum_ctrl = &p_hub_driver_obj->single_thread.enum_ctrl;\n'
                 '    risc_usb_enum_stage(enum_stage_strings[enum_ctrl->stage]);\n'
                 '    switch (enum_ctrl->stage)')
    # Replace original error sites, leaving the macro's own forwarding intact.
    if source.count('ESP_LOGE(HUB_DRIVER_TAG, "') != 14:
        raise ValueError('Pinned IDF enumeration error sites changed')
    source = source.replace('ESP_LOGE(HUB_DRIVER_TAG, "', 'RISC_USB_ENUM_ERROR("')
    return source
