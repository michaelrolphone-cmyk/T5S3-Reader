#include "T5AppApi.h"
#include <assert.h>
#include <string.h>
void app_main(void);
static int ticks, scenario, launched, labels;
static uint32_t chosen;
static int32_t width(void) { return 540; }
static int32_t height(void) { return 960; }
static void clear(void) {}
static void text(int32_t x,int32_t y,const char *s) { (void)x;(void)y;(void)s; }
static void rect(int32_t x,int32_t y,int32_t w,int32_t h,bool b) {
  (void)b; assert(x>=0 && y>=0 && x+w<=540 && y+h<=960);
}
static void present(bool full) { (void)full; }
static bool refresh(void) { return true; }
static uint32_t count(void) { return scenario == 3 ? 0 : 20; }
static bool get(uint32_t i,t5_app_manifest_t *m) {
  assert(i<count()); memset(m,0,sizeof(*m)); m->compatible=scenario!=2;
  strcpy(m->display_name,"Example app"); strcpy(m->icon,"solid:f013"); return true;
}
static bool launch(uint32_t i) { launched++; chosen=i; return true; }
static bool icon(int32_t x,int32_t y,const char *s,uint8_t size,bool black) {
  (void)x;(void)y;(void)s;(void)size;(void)black; return true;
}
static void label(int32_t x,int32_t y,int32_t w,const char *s) {
  (void)s; assert(x>=0 && x+w<=540 && y<960); labels++;
}
static bool poll(t5_app_input_t *in,uint32_t wait) {
  (void)wait; memset(in,0,sizeof(*in)); ticks++;
  if (ticks>3) { in->exit_requested=true; return true; }
  if (scenario==0) { in->tapped=true; in->touch_x=250; in->touch_y=130; }
  if (scenario==1) {
    if (ticks==1) { in->tapped=true; in->touch_x=400; in->touch_y=940; }
    if (ticks==2) in->buttons=T5_APP_BUTTON_CONFIRM;
  }
  if (scenario==2) in->buttons=T5_APP_BUTTON_CONFIRM;
  return true;
}
static const t5_app_api_v1 api={.abi_version=1,.struct_size=sizeof(t5_app_api_v1),
 .screen_width=width,.screen_height=height,.clear=clear,.draw_text=text,.fill_rect=rect,
 .present=present,.poll=poll,.installed_apps_refresh=refresh,.installed_apps_count=count,
 .installed_apps_get=get,.request_app_launch=launch,.draw_icon=icon,.draw_label=label};
const t5_app_api_v1 *t5_app_get_api(uint32_t v) { assert(v==1); return &api; }
int main(void) {
  for(scenario=0;scenario<4;scenario++) {
    ticks=launched=labels=0; chosen=0; app_main();
    if(scenario==0) assert(launched==1 && chosen==1);
    if(scenario==1) assert(launched==1 && chosen==15);
    if(scenario>=2) assert(launched==0);
    assert(labels>0);
  }
}
