#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "T5AppApi.h"
#include "T5FontApi.h"
#include "T5UiApi.h"

void app_main(void);
static int polls;
static int selected = -1;
static int renders;

static bool app_poll(t5_app_input_t *input, uint32_t wait_ms) {
    assert(input && wait_ms == 50);
    memset(input, 0, sizeof(*input));
    ++polls;
    if (polls == 1) input->buttons = T5_APP_BUTTON_DOWN;
    else input->buttons = T5_APP_BUTTON_CONFIRM;
    return true;
}
static const t5_app_api_v1 app_api = {.abi_version=T5_APP_ABI_VERSION,.struct_size=sizeof(t5_app_api_v1),.poll=app_poll};
const t5_app_api_v1 *t5_app_get_api(uint32_t v) { return v == T5_APP_ABI_VERSION ? &app_api : NULL; }

static uint32_t choice_count(void) { return 3; }
static bool choice_info(uint32_t i, t5_font_choice_info_t *out) {
    static const char *names[] = {"Noto Serif", "Noto Sans", "Literata"};
    assert(out && i < 3);
    memset(out, 0, sizeof(*out));
    strcpy(out->name, names[i]);
    out->builtin = i < 2;
    out->selected = i == 0;
    return true;
}
static t5_font_result_t select_choice(uint32_t i) { selected = (int)i; return T5_FONT_OK; }
static const t5_font_api_v1 font_api = {
    .api_version=T5_FONT_API_VERSION,.struct_size=sizeof(t5_font_api_v1),
    .choice_count=choice_count,.choice_info=choice_info,.select_choice=select_choice,
};
const t5_font_api_v1 *t5_font_get_api(uint32_t v) { return v == T5_FONT_API_VERSION ? &font_api : NULL; }

static void render_list(const t5_ui_chrome_t *chrome, const t5_ui_list_row_t *rows, uint32_t count, int32_t index) {
    assert(chrome && rows && count == 3 && index >= 0 && index < 3);
    assert(strcmp(chrome->title, "Font Family") == 0);
    ++renders;
}
static int32_t hit_test(int16_t x,int16_t y){(void)x;(void)y;return T5_UI_HIT_NONE;}
static int32_t next_index(int32_t current,uint32_t count){return (current+1)%(int32_t)count;}
static int32_t previous_index(int32_t current,uint32_t count){return current<=0?(int32_t)count-1:current-1;}
static const t5_ui_api_v1 ui_api={.api_version=T5_UI_API_VERSION,.struct_size=sizeof(t5_ui_api_v1),.render_list=render_list,.hit_test=hit_test,.next_index=next_index,.previous_index=previous_index};
const t5_ui_api_v1 *t5_ui_get_api(uint32_t v){return v==T5_UI_API_VERSION?&ui_api:NULL;}

int main(void){app_main();assert(selected==1);assert(polls==2);assert(renders==2);return 0;}
