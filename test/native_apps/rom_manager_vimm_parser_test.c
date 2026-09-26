#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define app_main rom_manager_app_main
#include "../../Apps/rom_manager.c"
#undef app_main

static const char kLetterHtml[] =
    "<table class=\"hovertable\">"
    "<tr><td><a href=\"/vault/46856\">Tetris</a></td>"
    "<td><a href=\"/vault/90001\">9</a></td></tr>"
    "<tr><td><a href=\"/vault/12345\">Wario Land</a></td>"
    "<td><a href=\"/vault/90002\">9</a></td></tr>"
    "</table>";
static bool served;
static uint32_t fake_millis(void){return 100u;}
static const t5_app_api_v1 kApp={
  .abi_version=T5_APP_ABI_VERSION,
  .struct_size=sizeof(t5_app_api_v1),
  .millis=fake_millis,
};

static bool fake_remove_file(const char *path){assert(path);return true;}
static const t5_storage_api_v1 kStorage={
  .api_version=T5_STORAGE_API_VERSION,
  .struct_size=sizeof(t5_storage_api_v1),
  .remove_file=fake_remove_file,
};
static bool fake_poll_event(t5_ui_event_t *event,uint32_t wait_ms){
  (void)wait_ms;assert(event);memset(event,0,sizeof(*event));return true;
}
static const t5_ui_api_v1 kUi={
  .api_version=T5_UI_API_VERSION,
  .struct_size=sizeof(t5_ui_api_v1),
  .poll_event=fake_poll_event,
};

static int32_t fake_open_http(const char *url,t5_stream_t *out){
  assert(url&&out);
  assert(strcmp(url,"https://vimm.net/vault/?p=list&system=GB&section=W")==0);
  served=false;*out=1;return T5_STREAM_OK;
}
static int32_t fake_read(t5_stream_t h,void *buffer,uint32_t cap,uint32_t *count){
  assert(h==1&&buffer&&count);
  if(served){*count=0;return T5_STREAM_EOF;}
  const size_t n=strlen(kLetterHtml);
  assert(n<=cap);
  memcpy(buffer,kLetterHtml,n);
  *count=(uint32_t)n;
  served=true;
  return T5_STREAM_EOF;
}
static int32_t fake_open_file(const char *path,uint32_t mode,t5_stream_t *out){
  assert(path&&out);assert(mode==T5_STREAM_FILE_CREATE_NEW);*out=2;return T5_STREAM_OK;
}
static int32_t fake_write(t5_stream_t h,const void *buffer,uint32_t size,uint32_t *count){
  assert(h==2&&buffer&&count);*count=size;return T5_STREAM_OK;
}
static int32_t fake_finish(t5_stream_t h){assert(h==2);return T5_STREAM_OK;}
static int32_t fake_close(t5_stream_t h){assert(h==1||h==2);return T5_STREAM_OK;}

static const t5_stream_api_v1 kStreams={
  .api_version=T5_STREAM_API_VERSION,
  .struct_size=sizeof(t5_stream_api_v1),
  .open_file=fake_open_file,
  .open_http=fake_open_http,
  .read=fake_read,
  .write=fake_write,
  .finish=fake_finish,
  .close=fake_close,
};

const t5_app_api_v1 *t5_app_get_api(uint32_t version){(void)version;return 0;}
const t5_archive_api_v1 *t5_archive_get_api(uint32_t version){(void)version;return 0;}
const t5_storage_api_v1 *t5_storage_get_api(uint32_t version){(void)version;return 0;}
const t5_stream_api_v1 *t5_stream_get_api(uint32_t version){(void)version;return 0;}
const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t version){(void)version;return 0;}
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version){(void)version;return 0;}

int main(void){
  char buffer[4096],details[4096],debug[DEBUG_CAP];
  html=buffer;
  detail_text=details;
  debug_text=debug;
  debug_text[0]=0;debug_size=0;
  app=&kApp;
  storage=&kStorage;
  streams=&kStreams;
  ui=&kUi;
  search_query[0]=0;
  status_text[0]=0;

  assert(open_vimm_page("/vault/GB/W"));
  assert(vimm_count==2);
  assert(strcmp(vimm_names[0],"Tetris")==0);
  assert(strcmp(vimm_paths[0],"/vault/46856")==0);
  assert(vimm_kinds[0]==VIMM_GAME);
  assert(strcmp(vimm_names[1],"Wario Land")==0);
  assert(strcmp(vimm_paths[1],"/vault/12345")==0);
  assert(vimm_kinds[1]==VIMM_GAME);
  assert(strstr(debug_text,"parser_summary")!=0);
  assert(strstr(debug_text,"numeric_first=2")!=0);
  return 0;
}
