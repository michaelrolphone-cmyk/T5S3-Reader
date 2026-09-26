#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define app_main rom_manager_app_main
#include "../../Apps/rom_manager.c"
#undef app_main

static const char kLetterHtml[] =
    "<table><tr>"
    "<td><a href=\"/vault/11111\">9</a></td>"
    "<td><a href=\"/vault/46856\"><canvas data-v=\"VGV0cmlz\"></canvas></a></td>"
    "</tr></table>";
static bool served;
static uint32_t fake_millis(void){return 100u;}
static const t5_app_api_v1 kApp={
  .abi_version=T5_APP_ABI_VERSION,
  .struct_size=sizeof(t5_app_api_v1),
  .millis=fake_millis,
};

static int32_t fake_open_http(const char *url,t5_stream_t *out){
  assert(url&&out);served=false;*out=1;return T5_STREAM_OK;
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
static int32_t fake_close(t5_stream_t h){assert(h==1);return T5_STREAM_OK;}

static const t5_stream_api_v1 kStreams={
  .api_version=T5_STREAM_API_VERSION,
  .struct_size=sizeof(t5_stream_api_v1),
  .open_http=fake_open_http,
  .read=fake_read,
  .close=fake_close,
};

const t5_app_api_v1 *t5_app_get_api(uint32_t version){(void)version;return 0;}
const t5_archive_api_v1 *t5_archive_get_api(uint32_t version){(void)version;return 0;}
const t5_storage_api_v1 *t5_storage_get_api(uint32_t version){(void)version;return 0;}
const t5_stream_api_v1 *t5_stream_get_api(uint32_t version){(void)version;return 0;}
const t5_system_ui_api_v1 *t5_system_ui_get_api(uint32_t version){(void)version;return 0;}
const t5_ui_api_v1 *t5_ui_get_api(uint32_t version){(void)version;return 0;}

int main(void){
  char buffer[4096];
  html=buffer;
  detail_text=buffer;
  app=&kApp;
  streams=&kStreams;
  search_query[0]=0;
  status_text[0]=0;

  assert(fetch_vimm_url("https://vimm.net/vault/GB/W",false,false));
  assert(vimm_count==1);
  assert(strcmp(vimm_names[0],"Tetris")==0);
  assert(strcmp(vimm_paths[0],"/vault/46856")==0);
  assert(vimm_kinds[0]==VIMM_GAME);
  return 0;
}
