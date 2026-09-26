#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define app_main rom_manager_app_main
#include "../../Apps/rom_manager.c"
#undef app_main

static const char kDetailHtml[] =
    "<!DOCTYPE html><html><head><title>The Vault: Castlevania: The Adventure (GB)</title></head>"
    "<body><form action=\"//dl3.vimm.net/\" id=\"dl-form\">"
    "<input type=\"hidden\" name=\"mediaId\" value=\"39985\">"
    "<input type=\"hidden\" name=\"token\" value=\"abc123\"></form></body></html>";

static const char kLetterHtml[] =
    "<table class=\"rounded centered cellpadding1 hovertable striped\">"
    "<tr><td><a href=\"/vault/999999\" style=\"display:none\">9</a>"
    "<a href= \"/vault/46856\">Tetris</a></td>"
    "<td><a href=\"/vault/?p=rating&amp;id=46856\">10.0</a></td></tr>"
    "<tr><td><a href=\"/vault/999999\" style=\"display:none\">9</a>"
    "<a href= \"/vault/12345\">Wario Land</a></td>"
    "<td><a href=\"/vault/?p=rating&amp;id=12345\">8.5</a></td></tr>"
    "</table>";
static uint32_t fake_millis(void){return 100u;}
static unsigned log_count;
static void fake_log_message(const char *message){assert(message);++log_count;}
static const t5_app_api_v1 kApp={
  .abi_version=T5_APP_ABI_VERSION,
  .struct_size=sizeof(t5_app_api_v1),
  .millis=fake_millis,
  .log_message=fake_log_message,
};

static bool header_value(const t5_http_header_t *headers,uint32_t count,
                              const char *name,const char **value){
  for(uint32_t i=0;i<count;++i){
    if(headers[i].name&&headers[i].value&&!strcmp(headers[i].name,name)){
      if(value)*value=headers[i].value;
      return true;
    }
  }
  return false;
}
static bool fake_http_request(const char *url,uint8_t method,
                              const t5_http_header_t *headers,uint32_t header_count,
                              const void *body,size_t body_size,const char *cert_pem,
                              uint32_t timeout_ms,char *response,size_t response_capacity,
                              t5_http_result_t *result){
  (void)body;(void)body_size;(void)cert_pem;
  assert(url&&headers&&response&&result);
  assert(method==T5_HTTP_METHOD_GET);
  assert(timeout_ms==30000u);
  const char *ua=0,*referer=0;
  assert(header_value(headers,header_count,"User-Agent",&ua));
  assert(header_value(headers,header_count,"Referer",&referer));
  assert(strstr(ua,"Mozilla/5.0")!=0);
  assert(strcmp(referer,"https://vimm.net/vault/GB")==0);
  const char *payload=0;
  if(!strcmp(url,"https://vimm.net/vault/?p=list&system=GB&section=W"))payload=kLetterHtml;
  else if(!strcmp(url,"https://vimm.net/vault/3016"))payload=kDetailHtml;
  else assert(!"unexpected URL");
  const size_t n=strlen(payload);
  assert(n+1u<=response_capacity);
  memcpy(response,payload,n+1u);
  *result=(t5_http_result_t){.transport_error=0,.status_code=200,.response_bytes=n,.flags=0};
  return true;
}
static const t5_network_api_v1 kNetwork={
  .api_version=T5_NETWORK_API_VERSION,
  .struct_size=sizeof(t5_network_api_v1),
  .http_request=fake_http_request,
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
  .write=fake_write,
  .finish=fake_finish,
  .close=fake_close,
};

const t5_app_api_v1 *t5_app_get_api(uint32_t version){(void)version;return 0;}
const t5_archive_api_v1 *t5_archive_get_api(uint32_t version){(void)version;return 0;}
const t5_network_api_v1 *t5_network_get_api(uint32_t version){(void)version;return 0;}
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
  network=&kNetwork;
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
  assert(strstr(debug_text,"href=/vault/999999 title=9 hidden=1")!=0);
  assert(strstr(debug_text,"selected_href=/vault/46856 selected_title=Tetris found=1")!=0);
  assert(log_count>0);

  assert(fetch_vimm_document("https://vimm.net/vault/3016"));
  assert(html_size==strlen(kDetailHtml));
  assert(strstr(html,"Castlevania: The Adventure")!=0);
  assert(strstr(debug_text,"status=200")!=0);
  return 0;
}
