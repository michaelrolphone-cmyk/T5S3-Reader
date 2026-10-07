#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define app_main rom_manager_app_main
#include "../../Apps/rom_manager.c"
#undef app_main

static const char kDetailHtml[] =
    "<!DOCTYPE html><html><head><title>The Vault: Adventure Island (GB)</title></head>"
    "<body><script>const allMedia=[];</script>"
    "<form action=\"//download4.vimm.net/download/\" id=\"download_form\">"
    "<input type=\"hidden\" name=\"mediaId\" value=\"2943\">"
    "<input type=\"hidden\" name=\"alt\" value=\"0\"></form></body></html>";
static const char kTokenDetailHtml[] =
    "<!DOCTYPE html><html><head><title>The Vault: Castlevania: The Adventure (GB)</title></head>"
    "<body><form action=\"//dl3.vimm.net/\" id=\"dl-form\">"
    "<input type=\"hidden\" name=\"mediaId\" value=\"39985\">"
    "<input type=\"hidden\" name=\"token\" value=\"abc123\"></form></body></html>";
static const char kNotFoundHtml[] =
    "<!DOCTYPE html><html><head><title>Page Not Found</title></head>"
    "<body><h1>404 - Page Not Found</h1></body></html>";

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
  const char *payload=0;
  int32_t status=200;
  uint8_t flags=0;
  if(!strcmp(url,"https://vimm.net/vault/?p=list&system=GB&section=W")){
    assert(strcmp(referer,"https://vimm.net/vault/GB")==0);
    payload=kLetterHtml;
    flags=T5_HTTP_SESSION_COOKIES_STORED;
  }else{
    assert(strcmp(referer,"https://vimm.net/vault/?p=list&system=GB&section=W")==0);
    flags=T5_HTTP_SESSION_COOKIES_AVAILABLE|T5_HTTP_SESSION_COOKIES_STORED;
    if(!strcmp(url,"https://vimm.net/vault/2943")){payload=kDetailHtml;status=404;}
    else if(!strcmp(url,"https://vimm.net/vault/3016"))payload=kTokenDetailHtml;
    else if(!strcmp(url,"https://vimm.net/vault/9998")){payload=kNotFoundHtml;status=404;}
    else assert(!"unexpected URL");
  }
  const size_t n=strlen(payload);
  assert(n+1u<=response_capacity);
  memcpy(response,payload,n+1u);
  *result=(t5_http_result_t){.transport_error=0,.status_code=status,.response_bytes=n,.flags=flags};
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

static const char *expected_import_url;
static char import_destination[PATH_CAP];
static unsigned import_requests, import_published, import_cleanups;
static bool import_http_fail, import_cancel;
static int32_t import_open_http(const char *url,t5_stream_t *out){
  assert(!strcmp(url,expected_import_url));++import_requests;
  if(import_http_fail)return T5_STREAM_IO;
  *out=1;return T5_STREAM_OK;
}
static int32_t import_connect(t5_stream_t src,t5_stream_t dst,uint32_t policy,t5_pipe_t *out){
  assert(src==1&&dst==2&&policy==T5_PIPE_BLOCK_PRODUCER);*out=3;return T5_STREAM_OK;
}
static int32_t import_info(t5_pipe_t pipe,t5_pipe_info_t *info){
  assert(pipe==3);info->bytes_transferred=128;info->state=T5_PIPE_DONE;return T5_STREAM_OK;
}
static int32_t import_pipe_end(t5_pipe_t pipe){assert(pipe==3);return T5_STREAM_OK;}
static bool import_poll(t5_ui_event_t *event,uint32_t wait){
  (void)wait;memset(event,0,sizeof(*event));if(import_cancel)event->type=T5_UI_EVENT_BACK;return true;
}
static bool import_remove(const char *path){assert(!strcmp(path,TMP_GB)||!strcmp(path,TMP_ZIP));++import_cleanups;return true;}
static t5_storage_stream_t import_raw_open(const char *path,size_t *size){assert(!strcmp(path,TMP_GB));*size=128;return 4;}
static void import_raw_close(t5_storage_stream_t handle){assert(handle==4);}
static bool import_rename(const char *source,const char *destination){
  assert(!strcmp(source,TMP_GB));copy_text(import_destination,sizeof(import_destination),destination);++import_published;return true;
}
static bool import_find(const char *path,const char *suffix,char *entry,size_t capacity,uint64_t *size){
  assert(!strcmp(path,TMP_ZIP)&&!strcmp(suffix,".gb"));copy_text(entry,capacity,"folder/archive.gb");*size=128;return true;
}
static bool import_extract(const char *path,const char *entry,const char *destination,uint64_t limit,uint32_t timeout,t5_archive_progress_fn progress,void *context){
  (void)progress;(void)context;assert(!strcmp(path,TMP_ZIP)&&!strcmp(entry,"folder/archive.gb"));assert(limit==MAX_ROM_BYTES&&timeout==60000u);
  copy_text(import_destination,sizeof(import_destination),destination);++import_published;return true;
}
static void test_import_urls(void){
  t5_stream_api_v1 stream_api=kStreams;
  stream_api.open_http=import_open_http;stream_api.pipe_connect=import_connect;stream_api.pipe_info=import_info;
  stream_api.pipe_close=import_pipe_end;stream_api.pipe_cancel=import_pipe_end;
  t5_storage_api_v1 storage_api=kStorage;
  storage_api.remove_file=import_remove;storage_api.stream_open=import_raw_open;storage_api.stream_close=import_raw_close;storage_api.rename_file=import_rename;
  t5_ui_api_v1 ui_api=kUi;ui_api.poll_event=import_poll;
  const t5_archive_api_v1 archive_api={.find_first_suffix=import_find,.extract_file=import_extract};
  streams=&stream_api;storage=&storage_api;ui=&ui_api;archive=&archive_api;
  const struct {const char *url,*name;} cases[]={
    {"https://example.test/game.gb?token=abc","game.gb"},
    {"https://example.test/Game.GB#section","Game.GB"},
    {"https://example.test/game.gb?next=/other/name.zip#x/y","game.gb"},
    {"https://example.test/game.zip?download=1","archive.gb"},
    {"https://example.test/Game.ZiP#section","archive.gb"},
    {"https://example.test/game.gb","game.gb"},
    {"https://example.test/game.zip","archive.gb"},
    {"https://example.test/game.gb?#","game.gb"},
  };
  for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
    expected_import_url=cases[i].url;unsigned before=import_requests, published=import_published;
    assert(import_url(expected_import_url));assert(import_requests==before+1&&import_published==published+1);
    char wanted[PATH_CAP];make_path(cases[i].name,wanted,sizeof(wanted));assert(!strcmp(import_destination,wanted));
  }
  const char *invalid[]={NULL,"", "http://example.test/game.gb", "https://example.test/download?name=game.gb", "https://example.test/game.gb/", "https://example.gb", "https://example.test?next=/game.gb", "https://example.test/#game.zip", "https://example.test/game.gb.exe?x=.gb"};
  for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){unsigned before=import_requests;assert(!import_url(invalid[i]));assert(import_requests==before);}
  expected_import_url=cases[0].url;unsigned published=import_published,cleanups=import_cleanups;
  import_http_fail=true;assert(!import_url(expected_import_url));assert(import_published==published&&import_cleanups>cleanups);
  import_http_fail=false;assert(import_url(expected_import_url));
  published=import_published;cleanups=import_cleanups;import_cancel=true;
  assert(!import_url(expected_import_url));assert(import_published==published&&import_cleanups>cleanups);
  import_cancel=false;assert(import_url(expected_import_url));
  streams=&kStreams;storage=&kStorage;ui=&kUi;archive=NULL;
}

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
  copy_text(vimm_referer,sizeof(vimm_referer),VIMM_URL);
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

  assert(fetch_vimm_detail_document("https://vimm.net/vault/2943"));
  assert(html_size==strlen(kDetailHtml));
  assert(strstr(html,"Adventure Island")!=0);
  assert(strstr(debug_text,"status=404")!=0);
  assert(strstr(debug_text,"detail_soft_404=1 accepted=1")!=0);
  assert(strstr(debug_text,"flags=0x06")!=0);
  assert(strstr(debug_text,"referer=https://vimm.net/vault/?p=list&system=GB&section=W")!=0);
  char download_url[512];
  assert(resolve_vimm_download_url(download_url,sizeof(download_url)));
  assert(strcmp(download_url,"https://download4.vimm.net/download/?mediaId=2943")==0);

  assert(fetch_vimm_document("https://vimm.net/vault/3016"));
  assert(html_size==strlen(kTokenDetailHtml));
  assert(strstr(html,"Castlevania: The Adventure")!=0);
  assert(resolve_vimm_download_url(download_url,sizeof(download_url)));
  assert(strcmp(download_url,"https://dl3.vimm.net/?mediaId=39985&token=abc123")==0);

  assert(!fetch_vimm_detail_document("https://vimm.net/vault/9998"));
  assert(strcmp(status_text,"Vimm HTTP 404")==0);
  assert(strstr(debug_text,"detail_soft_404=1 accepted=0")!=0);
  test_import_urls();
  return 0;
}
