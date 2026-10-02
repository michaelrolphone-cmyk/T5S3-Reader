#include "T5AppApi.h"
#include "T5StorageApi.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
void app_main(void);
static int ticks, scenario, launched, labels, writes, highlight_rects;
static int selection_underlines, edit_button_rects, edit_labels, edit_label_y, forbidden_labels, apps_titles, page_dot_rects;
static int white_icon_calls, presents;
static int tone_white_calls, tone_light_calls, tone_dark_calls, tone_black_calls;
static uint32_t chosen;
static char saved[4096];
static size_t saved_size;
static int32_t width(void) { return 540; }
static int32_t height(void) { return 960; }
static void clear(void) {}
static void text(int32_t x,int32_t y,const char *s) {
  (void)x; (void)y;
  if (s && !strcmp(s,"Apps")) apps_titles++;
}
static void rect(int32_t x,int32_t y,int32_t w,int32_t h,bool b) {
  (void)b;
  assert(x>=0 && y>=0 && x+w<=540 && y+h<=960);
  if (w > 100 && y >= 80 && y < 200) highlight_rects++;
  if (h == 3 && w > 100 && y >= 200) selection_underlines++;
  if (x >= 450 && y < 60 && w > 30) edit_button_rects++;
  if (y > 900 && w <= 10 && h <= 10) page_dot_rects++;
}
static void present(bool full) { (void)full; presents++; }
static uint32_t now_ms(void) { return (uint32_t)ticks * 20u; }
static bool refresh(void) { return true; }
static uint32_t count(void) {
  if (scenario == 3 || scenario == 14) return 0;
  if (scenario == 13) return 1;
  return scenario >= 6 ? 35 : 20;
}
static bool get(uint32_t i,t5_app_manifest_t *m) {
  assert(i<count()); memset(m,0,sizeof(*m)); m->compatible=scenario!=2;
  strcpy(m->display_name,"Example app"); strcpy(m->icon,"solid:f013");
  snprintf(m->file_name,sizeof(m->file_name),"app%u.elf",(unsigned)i); return true;
}
static bool launch(uint32_t i) { launched++; chosen=i; return true; }
static bool icon(int32_t x,int32_t y,const char *s,uint8_t size,bool black) {
  (void)x;(void)y;(void)s;(void)size;
  if (!black) white_icon_calls++;
  return true;
}
static void tone_rect(int32_t x,int32_t y,int32_t w,int32_t h,int32_t radius,uint8_t tone) {
  assert(x>=0 && y>=0 && x+w<=540 && y+h<=960 && radius>=0);
  if (tone == T5_APP_TONE_WHITE) tone_white_calls++;
  else if (tone == T5_APP_TONE_LIGHT_GRAY) tone_light_calls++;
  else if (tone == T5_APP_TONE_DARK_GRAY) tone_dark_calls++;
  else if (tone == T5_APP_TONE_BLACK) tone_black_calls++;
  else assert(0 && "invalid tone");
}
static void label(int32_t x,int32_t y,int32_t w,const char *s) {
  assert(x>=0 && x+w<=540 && y<960); labels++;
  if (!s) return;
  if (!strcmp(s,"EDIT") || !strcmp(s,"DONE")) { edit_labels++; edit_label_y=y; }
  if (!strcmp(s,"< Previous") || !strcmp(s,"Next >") ||
      strstr(s,"Tap app:") || strstr(s,"Tap apps to add/remove")) forbidden_labels++;
}
static bool poll(t5_app_input_t *in,uint32_t wait) {
  (void)wait; memset(in,0,sizeof(*in)); ticks++;
  if (ticks>(scenario >= 6 ? 4 : 3)) { in->exit_requested=true; return true; }
  if (scenario==0) { in->tapped=true; in->touch_x=250; in->touch_y=130; }
  if (scenario==1) {
    if (ticks==1) { in->tapped=true; in->touch_x=270; in->touch_y=932; }
    if (ticks==2) in->buttons=T5_APP_BUTTON_CONFIRM;
  }
  if (scenario==2) in->buttons=T5_APP_BUTTON_CONFIRM;
  if (scenario==4) {
    if (ticks==1) { in->tapped=true; in->touch_x=490; in->touch_y=32; }
    if (ticks==2) { in->tapped=true; in->touch_x=40; in->touch_y=110; }
    if (ticks==3) { in->tapped=true; in->touch_x=490; in->touch_y=32; }
  }
  if (scenario==5 && ticks==1) in->buttons=T5_APP_BUTTON_RIGHT;
  if (scenario >= 6 && scenario <= 14) {
    // Even a simultaneously reported tap must not activate a swiped icon.
    if (ticks == 1) { in->tapped=true; in->touch_x=250; in->touch_y=130; }
    if (ticks == 3) in->buttons=T5_APP_BUTTON_CONFIRM;
  }
  if (scenario == 15) {
    if (ticks == 1) { in->tapped=true; in->touch_x=490; in->touch_y=32; }
    if (ticks == 3) { in->tapped=true; in->touch_x=40; in->touch_y=110; }
  }
  if (scenario >= 16 && ticks == 3) in->buttons=T5_APP_BUTTON_CONFIRM;
  return true;
}
static bool take_swipe(t5_app_swipe_t *out) {
  if (scenario < 6) return false;
  if (scenario == 15 ? ticks != 2 : ticks != 1 && !((scenario == 8 || scenario == 9) && ticks == 2))
    return false;
  *out=(t5_app_swipe_t){400,300,100,300};
  if (scenario == 7 || (scenario == 8 && ticks == 2) || (scenario == 9 && ticks == 1))
    *out=(t5_app_swipe_t){100,300,400,300};
  if (scenario == 10) *out=(t5_app_swipe_t){200,600,200,200};
  if (scenario == 11) *out=(t5_app_swipe_t){400,600,100,300};
  if (scenario == 12) *out=(t5_app_swipe_t){200,300,170,300};
  return true;
}
static bool storage_exists(const char *path) {
  return saved_size && !strcmp(path,"/sd/Apps/.home_apps");
}
static bool storage_read(const char *path,void *buffer,size_t capacity,size_t *out) {
  if (!out || !storage_exists(path)) return false;
  *out=saved_size;
  if (!buffer || capacity==0) return true;
  if (capacity<saved_size) return false;
  memcpy(buffer,saved,saved_size); return true;
}
static bool storage_write(const char *path,const void *data,size_t size) {
  assert(!strcmp(path,"/sd/Apps/.home_apps"));
  assert(size<=sizeof(saved));
  if (size) {
    memcpy(saved,data,size);
  }
  saved_size=size;
  writes++;
  return true;
}
static bool storage_remove(const char *path) { (void)path; saved_size=0; return true; }
static const t5_storage_api_v1 storage_api={.api_version=1,.struct_size=sizeof(t5_storage_api_v1),
 .exists=storage_exists,.read_file=storage_read,.write_file_atomic=storage_write,.remove_file=storage_remove};
const t5_storage_api_v1 *t5_storage_get_api(uint32_t v) { assert(v==1); return &storage_api; }
static t5_app_api_v1 api={.abi_version=1,.struct_size=sizeof(t5_app_api_v1),
 .screen_width=width,.screen_height=height,.clear=clear,.draw_text=text,.fill_rect=rect,
 .present=present,.poll=poll,.millis=now_ms,.installed_apps_refresh=refresh,.installed_apps_count=count,
 .installed_apps_get=get,.request_app_launch=launch,.draw_icon=icon,.draw_label=label,
 .fill_rounded_rect_tone=tone_rect,.take_touch_swipe=take_swipe};
const t5_app_api_v1 *t5_app_get_api(uint32_t v) { assert(v==1); return &api; }
int main(void) {
  for(scenario=0;scenario<18;scenario++) {
    api.struct_size=scenario == 16 ? offsetof(t5_app_api_v1,take_touch_swipe) : sizeof(api);
    api.take_touch_swipe=scenario == 17 ? NULL : take_swipe;
    ticks=launched=labels=writes=highlight_rects=presents=0;
    selection_underlines=edit_button_rects=edit_labels=forbidden_labels=apps_titles=page_dot_rects=0;
    edit_label_y=-1;
    white_icon_calls=0;
    tone_white_calls=tone_light_calls=tone_dark_calls=tone_black_calls=0;
    chosen=0; saved_size=0; app_main();
    if(scenario==0) assert(launched==1 && chosen==1);
    if(scenario==1) assert(launched==1 && chosen==15);
    if(scenario==2 || scenario==3 || scenario==4 || scenario==5) assert(launched==0);
    if(scenario==0) assert(selection_underlines==0);
    if(scenario==5) assert(selection_underlines>0);
    if(scenario==4) {
      assert(writes==1);
      assert(saved_size==strlen("app0.elf\n"));
      assert(!memcmp(saved,"app0.elf\n",saved_size));
      assert(highlight_rects >= 2);
    }
    if (scenario >= 6) assert(selection_underlines == 0);
    if (scenario == 6) assert(launched == 1 && chosen == 15 && presents == 2);
    if (scenario == 7) assert(launched == 1 && chosen == 30 && presents == 2);
    if (scenario == 8) assert(launched == 1 && chosen == 0 && presents == 3);
    if (scenario == 9) assert(launched == 1 && chosen == 0 && presents == 3);
    if ((scenario >= 10 && scenario <= 13) || scenario >= 16)
      assert(launched == 1 && chosen == 0 && presents == 1);
    if (scenario == 14) assert(launched == 0 && presents == 1);
    if (scenario == 15) {
      assert(launched == 0 && writes == 1 && presents == 4);
      assert(saved_size == strlen("app15.elf\n"));
      assert(!memcmp(saved,"app15.elf\n",saved_size));
    }
    assert(labels>0);
    assert(apps_titles==0);
    assert(forbidden_labels==0);
    assert(edit_button_rects>0);
    assert(edit_labels>0);
    assert(edit_label_y==19);
    assert(page_dot_rects>0);
    if (count() != 0) {
      assert(white_icon_calls>0);
      assert(tone_white_calls>0);
      assert(tone_light_calls>0);
      assert(tone_dark_calls>0);
      assert(tone_black_calls>0);
    }
  }
}

#include "T5VideoApi.h"
const t5_video_api_v1* t5_video_get_api(uint32_t version) { (void)version; return NULL; }
