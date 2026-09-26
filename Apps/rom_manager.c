#include "T5AppApi.h"
#include "T5ArchiveApi.h"
#include "T5StorageApi.h"
#include "T5StreamApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ROM_DIR "/sd/System/State/Applications/Rom Manager"
#define TMP_ZIP ROM_DIR "/.download.zip"
#define TMP_GB ROM_DIR "/.download.gb"
#define RENAME_STATE ROM_DIR "/.rename.txt"
#define VIMM_DEBUG_LOG ROM_DIR "/vimm-debug.log"
#define VIMM_LAST_HTML ROM_DIR "/vimm-last.html"
#define VIMM_URL "https://vimm.net/vault/GB"
#define MAX_ROMS 128u
#define MAX_VIMM 96u
#define NAME_CAP 128u
#define PATH_CAP 384u
#define STATUS_CAP 160u
#define DETAIL_CAP 8192u
#define DEBUG_CAP 8192u
#define HTML_CAP (128u * 1024u)
#define MAX_DOWNLOAD_BYTES (32u * 1024u * 1024u)
#define MAX_ROM_BYTES (16u * 1024u * 1024u)
#define COOKIE_SEARCH 0x524f4d5345415243ULL
#define COOKIE_IMPORT 0x524f4d494d504f52ULL
#define COOKIE_RENAME 0x524f4d52454e414dULL

typedef enum { VIEW_HOME, VIEW_ROMS, VIEW_VIMM } view_t;
static const t5_app_api_v1 *app;
static const t5_archive_api_v1 *archive;
static const t5_storage_api_v1 *storage;
static const t5_stream_api_v1 *streams;
static const t5_system_ui_api_v1 *system_ui;
static const t5_ui_api_v1 *ui;
static char status_text[STATUS_CAP];
static char rom_names[MAX_ROMS][NAME_CAP];
static uint64_t rom_sizes[MAX_ROMS];
static uint32_t rom_count;
static char vimm_names[MAX_VIMM][NAME_CAP];
static char vimm_paths[MAX_VIMM][NAME_CAP];
static uint8_t vimm_kinds[MAX_VIMM];
static uint32_t vimm_count;
enum { VIMM_GAME = 1u, VIMM_PAGE = 2u };
static char *html;
static uint32_t html_size;
static char search_query[80];
static char *detail_text;
static char *debug_text;
static size_t debug_size;

static size_t bounded_len(const char *s, size_t cap) { size_t n=0; if(s) while(n<cap&&s[n])++n; return n; }
static void copy_text(char *d,size_t c,const char*s){ if(!d||!c)return; if(!s)s=""; size_t n=bounded_len(s,c-1); memcpy(d,s,n); d[n]=0; }
static char lower_ascii(char c){return c>='A'&&c<='Z'?(char)(c+32):c;}
static bool ends_ci(const char *s,const char *suffix){size_t n=strlen(s),m=strlen(suffix);if(n<m)return false;for(size_t i=0;i<m;i++)if(lower_ascii(s[n-m+i])!=lower_ascii(suffix[i]))return false;return true;}
static bool contains_ci(const char *s,const char *q){if(!q||!q[0])return true;size_t m=strlen(q);for(size_t i=0;s&&s[i];++i){size_t j=0;while(j<m&&s[i+j]&&lower_ascii(s[i+j])==lower_ascii(q[j]))++j;if(j==m)return true;}return false;}
static void safe_name(const char *src,char *dst,size_t cap,const char *fallback){
  const char *base=src; for(const char*p=src;p&&*p;++p) if(*p=='/'||*p=='\\')base=p+1;
  size_t w=0; for(size_t i=0;base&&base[i]&&w+1<cap;++i){char c=base[i]; if(c=='?'||c=='#')break; if((unsigned char)c<32||c=='/'||c=='\\'||c==':'||c=='*'||c=='"'||c=='<'||c=='>'||c=='|')c='_'; dst[w++]=c;}
  dst[w]=0; if(!w)copy_text(dst,cap,fallback);
}
static void make_path(const char *name,char *out,size_t cap){snprintf(out,cap,"%s/%s",ROM_DIR,name);}
static void ensure_gb_suffix(char *name,size_t cap){
  if(!name||!cap||ends_ci(name,".gb"))return;
  size_t n=bounded_len(name,cap);
  if(n+3u>=cap)return;
  name[n++]='.';name[n++]='g';name[n++]='b';name[n]=0;
}
static bool ensure_rom_dir(void){
  static const char empty[]="";
  if(storage->exists(ROM_DIR))return true;
  if(!storage->write_file_atomic(ROM_DIR "/.init",empty,0))return false;
  (void)storage->remove_file(ROM_DIR "/.init");
  return true;
}
static bool allocate_workspaces(void){
  html=(char*)app->psram_alloc(HTML_CAP+1u);
  if(!html)return false;
  detail_text=(char*)app->psram_alloc(DETAIL_CAP);
  if(!detail_text){
    app->psram_free(html);html=NULL;return false;
  }
  debug_text=(char*)app->psram_alloc(DEBUG_CAP);
  if(!debug_text){
    app->psram_free(detail_text);detail_text=NULL;
    app->psram_free(html);html=NULL;return false;
  }
  html[0]=0;detail_text[0]=0;debug_text[0]=0;debug_size=0;return true;
}
static void release_workspaces(void){
  if(debug_text){app->psram_free(debug_text);debug_text=NULL;debug_size=0;}
  if(detail_text){app->psram_free(detail_text);detail_text=NULL;}
  if(html){app->psram_free(html);html=NULL;}
}
static void debug_append(const char *fmt,...){
  if(!debug_text||debug_size+1u>=DEBUG_CAP||!fmt)return;
  va_list args;va_start(args,fmt);
  int n=vsnprintf(debug_text+debug_size,DEBUG_CAP-debug_size,fmt,args);
  va_end(args);
  if(n<=0)return;
  size_t wrote=(size_t)n;
  if(wrote>=DEBUG_CAP-debug_size)debug_size=DEBUG_CAP-1u;
  else debug_size+=wrote;
  debug_text[debug_size]=0;
}
static void debug_reset(const char *operation,const char *url){
  if(!debug_text)return;
  debug_size=0;debug_text[0]=0;
  debug_append("Rom Manager Vimm diagnostics\noperation=%s\nurl=%s\n",
               operation?operation:"unknown",url?url:"");
}
static bool write_debug_snapshot(const char *path,const char *data,size_t size){
  if(!path||(!data&&size))return false;
  (void)storage->remove_file(path);
  t5_stream_t out=0;
  if(streams->open_file(path,T5_STREAM_FILE_CREATE_NEW,&out)!=T5_STREAM_OK)return false;
  size_t offset=0;
  while(offset<size){
    uint32_t wrote=0;
    const uint32_t chunk=(uint32_t)((size-offset)>T5_STREAM_CHUNK?T5_STREAM_CHUNK:(size-offset));
    const int32_t rc=streams->write(out,data+offset,chunk,&wrote);
    if(rc!=T5_STREAM_OK||!wrote){streams->close(out);return false;}
    offset+=wrote;
  }
  const bool ok=streams->finish(out)==T5_STREAM_OK;
  streams->close(out);
  return ok;
}
static void debug_flush(void){
  if(debug_text&&debug_size)(void)write_debug_snapshot(VIMM_DEBUG_LOG,debug_text,debug_size);
}
static uint32_t count_marker(const char *text,const char *needle){
  if(!text||!needle||!needle[0])return 0;
  uint32_t count=0;const size_t n=strlen(needle);const char *p=text;
  while((p=strstr(p,needle))){++count;p+=n;}
  return count;
}
static void debug_html_preview(void){
  if(!html||!html_size)return;
  char preview[321];size_t w=0;
  for(size_t i=0;i<html_size&&w+1u<sizeof(preview);++i){
    char c=html[i];
    if(c=='\r'||c=='\n'||c=='\t')c=' ';
    if((unsigned char)c<32u)c='?';
    if(c==' '&&w&&preview[w-1]==' ')continue;
    preview[w++]=c;
  }
  preview[w]=0;
  debug_append("preview=%s\n",preview);
}
static void show_vimm_diagnostics(void){
  const char *text=(debug_text&&debug_text[0])?debug_text:
      "No Vimm diagnostics are available in this app session.\n"
      "Persistent files:\n"
      "/System/State/Applications/Rom Manager/vimm-debug.log\n"
      "/System/State/Applications/Rom Manager/vimm-last.html";
  int32_t scroll=0;
  for(;;){
    t5_ui_text_view_result_t result={0};
    const t5_ui_chrome_t chrome={"Vimm diagnostics","Rom Manager",
      "Raw response: vimm-last.html","Back","","Up","Down"};
    ui->render_text_view(&chrome,text,scroll,&result);
    t5_ui_event_t event={0};
    if(!ui->poll_event(&event,20)||event.type==T5_UI_EVENT_BACK||event.type==T5_UI_EVENT_EXIT)return;
    if(event.type==T5_UI_EVENT_PREVIOUS&&scroll<result.max_scroll_lines)++scroll;
    else if(event.type==T5_UI_EVENT_NEXT&&scroll>0)--scroll;
  }
}
static void render_rows(const char *title,const char *subtitle,const t5_ui_list_row_t *rows,uint32_t count,int32_t selected,const char *confirm){
  const t5_ui_chrome_t chrome={title,subtitle,status_text,"Back",confirm,"Up","Down"};
  if(count)ui->render_list(&chrome,rows,count,selected); else {const t5_ui_list_row_t empty={"Nothing here","","",0};ui->render_list(&chrome,&empty,1,0);}
}
static void load_roms(void){
  rom_count=0; if(!app->dir_open(ROM_DIR)) return; t5_app_dirent_t e={0};
  while(rom_count<MAX_ROMS&&app->dir_next(&e)){if(e.is_directory||!ends_ci(e.name,".gb"))continue;copy_text(rom_names[rom_count],NAME_CAP,e.name);rom_sizes[rom_count++]=e.size;}
  app->dir_close();
}
static bool stream_download(const char *url,const char *destination){
  storage->remove_file(destination);
  t5_stream_t src=0,dst=0; t5_pipe_t pipe=0;
  if(streams->open_file(destination,T5_STREAM_FILE_CREATE_NEW,&dst)!=T5_STREAM_OK)return false;
  if(streams->open_http(url,&src)!=T5_STREAM_OK){streams->close(dst);storage->remove_file(destination);return false;}
  if(streams->pipe_connect(src,dst,T5_PIPE_BLOCK_PRODUCER,&pipe)!=T5_STREAM_OK){streams->close(src);streams->close(dst);storage->remove_file(destination);return false;}
  uint64_t last=0; uint32_t started=app->millis(),last_progress=started,last_render=0; bool ok=false;
  for(;;){
    t5_pipe_info_t info={0}; info.struct_size=sizeof(info);
    if(streams->pipe_info(pipe,&info)!=T5_STREAM_OK)break;
    if(info.bytes_transferred>MAX_DOWNLOAD_BYTES){streams->pipe_cancel(pipe);break;}
    if(info.bytes_transferred!=last){last=info.bytes_transferred;last_progress=app->millis();}
    uint32_t now=app->millis();
    if(now-last_render>=300u){snprintf(status_text,sizeof(status_text),"Downloading: %llu KB",(unsigned long long)(last/1024u));const t5_ui_list_row_t r={"Download in progress","Back cancels","",0};render_rows("Rom Manager","Authorized ROM import",&r,1,0,"");last_render=now;}
    t5_ui_event_t event={0}; if(!ui->poll_event(&event,10)||event.type==T5_UI_EVENT_EXIT||event.type==T5_UI_EVENT_BACK){streams->pipe_cancel(pipe);break;}
    if(now-last_progress>30000u||now-started>180000u){streams->pipe_cancel(pipe);break;}
    if(info.state==T5_PIPE_DONE&&!info.buffered&&!info.last_error&&last){if(streams->pipe_close(pipe)==T5_STREAM_OK){pipe=0;if(streams->finish(dst)==T5_STREAM_OK)ok=true;}break;}
    if(info.state==T5_PIPE_FAILED||info.state==T5_PIPE_CANCELLED)break;
  }
  if(pipe)streams->pipe_close(pipe);
  streams->close(src);
  streams->close(dst);
  if(!ok)storage->remove_file(destination);
  return ok;
}
static bool extraction_progress(void *ctx,uint64_t done,uint64_t total){
  (void)ctx;
  static uint32_t last_bucket=UINT32_MAX;
  uint32_t bucket=total?(uint32_t)((done>total?total:done)*10u/total):0;
  if(bucket==last_bucket&&done!=total)return true;
  last_bucket=bucket;
  snprintf(status_text,sizeof(status_text),"Extracting: %llu / %llu KB",(unsigned long long)(done/1024u),(unsigned long long)(total/1024u));
  const t5_ui_list_row_t row={"Extracting ROM","ZIP -> .gb","",0};
  render_rows("Rom Manager","Authorized ROM import",&row,1,0,"");
  t5_ui_event_t event={0};
  if(!ui->poll_event(&event,1))return false;
  return event.type!=T5_UI_EVENT_BACK&&event.type!=T5_UI_EVENT_EXIT;
}
static bool import_url(const char *url){
  if(!url||strncmp(url,"https://",8)!=0){copy_text(status_text,sizeof(status_text),"Only HTTPS URLs are accepted");return false;}
  const bool zipped=ends_ci(url,".zip");
  if(!zipped&&!ends_ci(url,".gb")){copy_text(status_text,sizeof(status_text),"URL must end in .zip or .gb");return false;}
  const char *tmp=zipped?TMP_ZIP:TMP_GB;
  copy_text(status_text,sizeof(status_text),"Starting download...");
  if(!stream_download(url,tmp)){copy_text(status_text,sizeof(status_text),"Download failed or cancelled");return false;}
  char out_name[NAME_CAP],out_path[PATH_CAP];
  if(zipped){
    uint64_t size=0; char entry[NAME_CAP]={0};
    if(!archive->find_first_suffix(tmp,".gb",entry,sizeof(entry),&size)||!size||size>MAX_ROM_BYTES){storage->remove_file(tmp);copy_text(status_text,sizeof(status_text),"ZIP contains no supported .gb ROM");return false;}
    safe_name(entry,out_name,sizeof(out_name),"game.gb"); make_path(out_name,out_path,sizeof(out_path));
    if(!archive->extract_file(tmp,entry,out_path,MAX_ROM_BYTES,60000u,extraction_progress,NULL)){storage->remove_file(tmp);copy_text(status_text,sizeof(status_text),"ZIP extraction failed or target exists");return false;}
    storage->remove_file(tmp);
  }else{
    size_t raw_size=0; t5_storage_stream_t raw=storage->stream_open(tmp,&raw_size);
    if(raw==T5_STORAGE_STREAM_INVALID||raw_size==0||raw_size>MAX_ROM_BYTES){
      if(raw!=T5_STORAGE_STREAM_INVALID)storage->stream_close(raw);
      storage->remove_file(tmp);copy_text(status_text,sizeof(status_text),"Downloaded .gb has an invalid size");return false;
    }
    storage->stream_close(raw);
    const char *slash=strrchr(url,'/'); safe_name(slash?slash+1:url,out_name,sizeof(out_name),"game.gb"); ensure_gb_suffix(out_name,sizeof(out_name));
    make_path(out_name,out_path,sizeof(out_path));
    if(!storage->rename_file(tmp,out_path)){storage->remove_file(tmp);copy_text(status_text,sizeof(status_text),"ROM already exists or rename failed");return false;}
  }
  snprintf(status_text,sizeof(status_text),"Imported %.100s",out_name); return true;
}
static bool import_archive_url(const char *url){
  if(!url||strncmp(url,"https://",8)!=0){
    copy_text(status_text,sizeof(status_text),"Resolved ROM URL is not HTTPS");
    return false;
  }
  copy_text(status_text,sizeof(status_text),"Downloading ROM archive...");
  if(!stream_download(url,TMP_ZIP)){
    copy_text(status_text,sizeof(status_text),"ROM archive download failed or cancelled");
    return false;
  }
  uint64_t size=0; char entry[NAME_CAP]={0};
  if(!archive->find_first_suffix(TMP_ZIP,".gb",entry,sizeof(entry),&size)||!size||size>MAX_ROM_BYTES){
    storage->remove_file(TMP_ZIP);
    copy_text(status_text,sizeof(status_text),"Downloaded archive contains no supported .gb ROM");
    return false;
  }
  char out_name[NAME_CAP],out_path[PATH_CAP];
  safe_name(entry,out_name,sizeof(out_name),"game.gb");
  make_path(out_name,out_path,sizeof(out_path));
  if(!archive->extract_file(TMP_ZIP,entry,out_path,MAX_ROM_BYTES,60000u,extraction_progress,NULL)){
    storage->remove_file(TMP_ZIP);
    copy_text(status_text,sizeof(status_text),"ROM extraction failed or ROM already exists");
    return false;
  }
  storage->remove_file(TMP_ZIP);
  snprintf(status_text,sizeof(status_text),"Installed %.100s",out_name);
  return true;
}
static bool copy_url_candidate(const char *start,const char *end,char *out,size_t cap){
  if(!start||!end||start>=end||!out||cap<16u)return false;
  size_t len=(size_t)(end-start);
  if(len>=cap)return false;
  if(len>=8u&&!strncmp(start,"https://",8u)){
    memcpy(out,start,len);out[len]=0;return true;
  }
  const char host[]="https://vimm.net";
  const size_t host_len=sizeof(host)-1u;
  if(*start=='/'){
    if(host_len+len>=cap)return false;
    memcpy(out,host,host_len);memcpy(out+host_len,start,len);out[host_len+len]=0;return true;
  }
  if(*start=='?'){
    const char vault[]="https://vimm.net/vault/";
    const size_t vault_len=sizeof(vault)-1u;
    if(vault_len+len>=cap)return false;
    memcpy(out,vault,vault_len);memcpy(out+vault_len,start,len);out[vault_len+len]=0;return true;
  }
  return false;
}
static bool likely_download_target(const char *start,const char *end){
  const size_t len=(size_t)(end-start);
  if(len<4u)return false;
  for(size_t i=0;i+4u<=len;++i){
    if((i+4u<=len&&start[i]=='.'&&lower_ascii(start[i+1])=='z'&&lower_ascii(start[i+2])=='i'&&lower_ascii(start[i+3])=='p')||
       (i+3u<=len&&start[i]=='.'&&lower_ascii(start[i+1])=='g'&&lower_ascii(start[i+2])=='b'))return true;
  }
  for(size_t i=0;i+8u<=len;++i){
    if(!strncmp(start+i,"download",8u))return true;
  }
  for(size_t i=0;i+10u<=len;++i){
    if(!strncmp(start+i,"p=download",10u))return true;
  }
  return false;
}
static bool append_url_encoded(char *out,size_t cap,const char *text);
static bool range_contains(const char *start,const char *end,const char *needle){
  if(!start||!end||start>=end||!needle||!needle[0])return false;
  const size_t nlen=strlen(needle);
  for(const char *p=start;p+nlen<=end;++p)if(!strncmp(p,needle,nlen))return true;
  return false;
}
static bool copy_attr_value(const char *tag_start,const char *tag_end,const char *attr,
                            char *out,size_t cap){
  if(!tag_start||!tag_end||tag_start>=tag_end||!attr||!out||cap<2u)return false;
  char pattern[40];
  int n=snprintf(pattern,sizeof(pattern),"%s=\"",attr);
  if(n<=0||(size_t)n>=sizeof(pattern))return false;
  const char *p=tag_start;
  while(p<tag_end){
    const char *hit=strstr(p,pattern);
    if(!hit||hit>=tag_end)return false;
    const char *value=hit+n;
    const char *end=strchr(value,'"');
    if(!end||end>tag_end)return false;
    const size_t len=(size_t)(end-value);
    if(len>=cap)return false;
    memcpy(out,value,len);out[len]=0;return true;
  }
  return false;
}
static bool normalize_form_action(const char *action,char *out,size_t cap){
  if(!action||!action[0]||!out||cap<16u)return false;
  if(!strncmp(action,"https://",8u)){
    if(strlen(action)>=cap)return false;
    copy_text(out,cap,action);return true;
  }
  if(!strncmp(action,"//",2u)){
    const char prefix[]="https:";
    const size_t plen=sizeof(prefix)-1u,len=strlen(action);
    if(plen+len>=cap)return false;
    memcpy(out,prefix,plen);memcpy(out+plen,action,len+1u);return true;
  }
  if(action[0]=='/'){
    const char prefix[]="https://vimm.net";
    const size_t plen=sizeof(prefix)-1u,len=strlen(action);
    if(plen+len>=cap)return false;
    memcpy(out,prefix,plen);memcpy(out+plen,action,len+1u);return true;
  }
  return false;
}
static bool find_form_input_value(const char *form_start,const char *form_end,
                                  const char *name,char *out,size_t cap){
  const char *p=form_start;
  while((p=strstr(p,"<input"))&&p<form_end){
    const char *tag_end=strchr(p,'>');
    if(!tag_end||tag_end>form_end)return false;
    char input_name[40];
    if(copy_attr_value(p,tag_end,"name",input_name,sizeof(input_name))&&!strcmp(input_name,name))
      return copy_attr_value(p,tag_end,"value",out,cap);
    p=tag_end+1;
  }
  return false;
}
static bool resolve_vimm_dl_form(char *out,size_t cap){
  const char *p=html;
  while((p=strstr(p,"<form"))){
    const char *tag_end=strchr(p,'>');
    if(!tag_end)return false;
    if(!range_contains(p,tag_end,"id=\"dl-form\"")){p=tag_end+1;continue;}
    const char *form_end=strstr(tag_end,"</form>");
    if(!form_end)return false;
    char action[192],media_id[32],token[256],base[256],encoded_id[96],encoded_token[768];
    if(!copy_attr_value(p,tag_end,"action",action,sizeof(action))||
       !find_form_input_value(tag_end,form_end,"mediaId",media_id,sizeof(media_id))||
       !find_form_input_value(tag_end,form_end,"token",token,sizeof(token))||
       !normalize_form_action(action,base,sizeof(base))||
       !append_url_encoded(encoded_id,sizeof(encoded_id),media_id)||
       !append_url_encoded(encoded_token,sizeof(encoded_token),token))return false;
    const char separator=strchr(base,'?')?'&':'?';
    int n=snprintf(out,cap,"%s%cmediaId=%s&token=%s",base,separator,encoded_id,encoded_token);
    return n>0&&(size_t)n<cap;
  }
  return false;
}

static bool resolve_vimm_download_url(char *out,size_t cap){
  if(!out||cap<32u)return false;
  out[0]=0;
  /* Vimm title pages use form#dl-form. submitDL() changes that form to GET;
     resolve its action + hidden mediaId/token deterministically before any
     generic fallback scanning. */
  if(resolve_vimm_dl_form(out,cap))return true;
  const char *attrs[]={"href=\"","action=\"","data-href=\"","data-url=\""};
  for(size_t a=0;a<sizeof(attrs)/sizeof(attrs[0]);++a){
    const char *p=html;
    const size_t attr_len=strlen(attrs[a]);
    while((p=strstr(p,attrs[a]))){
      const char *start=p+attr_len;
      const char *end=strchr(start,'"');
      if(!end)break;
      if(likely_download_target(start,end)&&copy_url_candidate(start,end,out,cap))return true;
      p=end+1;
    }
  }
  return false;
}

static bool vimm_page_path(const char *path,size_t length){
  const char prefix[]="/vault/GB";
  const size_t prefix_len=sizeof(prefix)-1u;
  if(length<prefix_len||strncmp(path,prefix,prefix_len)!=0)return false;
  return length==prefix_len||path[prefix_len]=='/'||path[prefix_len]=='?';
}
static bool vimm_game_path(const char *path,size_t length){
  if(length<=7u||strncmp(path,"/vault/",7u)!=0)return false;
  for(size_t i=7u;i<length;++i)if(path[i]<'0'||path[i]>'9')return false;
  return true;
}
static bool vimm_allowed_url(const char *url){
  if(!url)return false;
  if(strncmp(url,VIMM_URL,strlen(VIMM_URL))==0)return true;
  const char list_url[]="https://vimm.net/vault/?p=list&system=GB";
  if(!strncmp(url,list_url,sizeof(list_url)-1u))return true;
  const char detail_prefix[]="https://vimm.net/vault/";
  const size_t detail_len=sizeof(detail_prefix)-1u;
  if(strncmp(url,detail_prefix,detail_len)!=0)return false;
  const char *id=url+detail_len;
  if(!*id)return false;
  while(*id){if(*id<'0'||*id>'9')return false;++id;}
  return true;
}
static bool fetch_vimm_document(const char *url){
  debug_reset("fetch",url);
  if(!vimm_allowed_url(url)){
    debug_append("allowlist=reject\n");
    debug_flush();
    copy_text(status_text,sizeof(status_text),"Blocked unsupported Vimm URL");return false;
  }
  debug_append("allowlist=accept\n");
  html_size=0;t5_stream_t h=0;
  const int32_t open_rc=streams->open_http(url,&h);
  debug_append("open_http=%ld handle=%lu\n",(long)open_rc,(unsigned long)h);
  if(open_rc!=T5_STREAM_OK){
    debug_flush();
    copy_text(status_text,sizeof(status_text),"Could not open Vimm HTTPS stream");
    return false;
  }
  uint32_t last_progress=app->millis(),started=last_progress,reads=0;
  while(html_size<HTML_CAP){
    uint32_t got=0;int32_t r=streams->read(h,html+html_size,(uint32_t)(HTML_CAP-html_size),&got);
    ++reads;
    if(got){html_size+=got;last_progress=app->millis();}
    if(r==T5_STREAM_EOF){debug_append("read_terminal=EOF reads=%lu\n",(unsigned long)reads);break;}
    if(r<0&&r!=T5_STREAM_AGAIN){
      debug_append("read_terminal=error rc=%ld reads=%lu bytes=%lu\n",
                   (long)r,(unsigned long)reads,(unsigned long)html_size);
      streams->close(h);debug_flush();
      copy_text(status_text,sizeof(status_text),"Vimm HTTPS read failed");
      return false;
    }
    t5_ui_event_t ev={0};
    if(!ui->poll_event(&ev,5)){
      debug_append("read_terminal=ui_poll_failed bytes=%lu\n",(unsigned long)html_size);
      streams->close(h);debug_flush();
      copy_text(status_text,sizeof(status_text),"UI poll failed during Vimm fetch");return false;
    }
    if(ev.type==T5_UI_EVENT_EXIT||ev.type==T5_UI_EVENT_BACK){
      debug_append("read_terminal=cancel event=%u bytes=%lu\n",(unsigned)ev.type,(unsigned long)html_size);
      streams->close(h);debug_flush();
      copy_text(status_text,sizeof(status_text),"Vimm fetch cancelled");return false;
    }
    uint32_t now=app->millis();
    if(now-last_progress>30000u||now-started>60000u){
      debug_append("read_terminal=timeout bytes=%lu elapsed=%lu\n",
                   (unsigned long)html_size,(unsigned long)(now-started));
      streams->close(h);debug_flush();
      copy_text(status_text,sizeof(status_text),"Vimm HTTPS fetch timed out");return false;
    }
  }
  streams->close(h);html[html_size]=0;
  debug_append("bytes=%lu capped=%u\n",(unsigned long)html_size,html_size==HTML_CAP?1u:0u);
  if(html_size){
    debug_append("markers table=%lu tr=%lu td=%lu hovertable=%lu vault_href=%lu data_v=%lu\n",
      (unsigned long)count_marker(html,"<table"),
      (unsigned long)count_marker(html,"<tr"),
      (unsigned long)count_marker(html,"<td"),
      (unsigned long)count_marker(html,"hovertable"),
      (unsigned long)count_marker(html,"href=\"/vault/"),
      (unsigned long)count_marker(html,"data-v=\""));
    debug_html_preview();
    const bool html_saved=write_debug_snapshot(VIMM_LAST_HTML,html,html_size);
    debug_append("saved_last_html=%u\n",html_saved?1u:0u);
  }
  if(!html_size){
    debug_append("result=empty_response\n");debug_flush();
    copy_text(status_text,sizeof(status_text),"Vimm returned an empty response");return false;
  }
  debug_flush();
  return true;
}
static bool decode_anchor_text(const char *start,const char *end,char *out,size_t cap){
  if(!start||!end||start>=end||!out||cap<2u)return false;
  size_t w=0; bool in_tag=false;
  for(const char *p=start;p<end&&w+1u<cap;){
    if(!in_tag&&*p=='<'){in_tag=true;++p;continue;}
    if(in_tag){if(*p=='>')in_tag=false;++p;continue;}
    if(*p=='&'){
      if(end-p>=5&&!strncmp(p,"&amp;",5)){out[w++]='&';p+=5;continue;}
      if(end-p>=6&&!strncmp(p,"&quot;",6)){out[w++]='"';p+=6;continue;}
      if(end-p>=5&&!strncmp(p,"&#39;",5)){out[w++]='\'';p+=5;continue;}
      if(end-p>=6&&!strncmp(p,"&nbsp;",6)){if(w&&out[w-1]!=' ')out[w++]=' ';p+=6;continue;}
    }
    char c=*p++;
    if(c=='\r'||c=='\n'||c=='\t')c=' ';
    if(c==' '&&(!w||out[w-1]==' '))continue;
    out[w++]=c;
  }
  while(w&&out[w-1]==' ')--w;
  out[w]=0;
  return w>0;
}
static bool vimm_path_seen(const char *path){
  for(uint32_t i=0;i<vimm_count;++i)if(!strcmp(vimm_paths[i],path))return true;
  return false;
}
static bool add_vimm_entry(const char *path,size_t plen,const char *title,uint8_t kind,bool server_filtered){
  if(!path||!title||!title[0]||plen>=NAME_CAP||vimm_count>=MAX_VIMM)return false;
  char path_copy[NAME_CAP];
  memcpy(path_copy,path,plen);path_copy[plen]=0;
  if(vimm_path_seen(path_copy))return false;
  if(!server_filtered&&!contains_ci(title,search_query))return false;
  copy_text(vimm_paths[vimm_count],NAME_CAP,path_copy);
  copy_text(vimm_names[vimm_count],NAME_CAP,title);
  vimm_kinds[vimm_count]=kind;
  ++vimm_count;
  return true;
}
static bool fetch_vimm_url(const char *url,bool include_pages,bool server_filtered){
  vimm_count=0;
  if(!fetch_vimm_document(url))return false;
  debug_append("parser include_pages=%u server_filtered=%u query=%s\n",
               include_pages?1u:0u,server_filtered?1u:0u,search_query);

  uint32_t nav_candidates=0,row_count=0,first_td_count=0,first_anchor_count=0;
  uint32_t numeric_first_anchor_count=0,sampled=0;

  if(include_pages){
    char *p=html;
    while(vimm_count<MAX_VIMM&&(p=strstr(p,"href=\"/vault/GB"))){
      p+=6;char *q=strchr(p,'"');if(!q)break;
      const size_t plen=(size_t)(q-p);
      if(plen>=NAME_CAP||!vimm_page_path(p,plen)){p=q+1;continue;}
      char *gt=strchr(q,'>');if(!gt)break;
      char *close=strstr(gt+1,"</a>");if(!close){p=gt+1;continue;}
      char title[NAME_CAP];
      if(decode_anchor_text(gt+1,close,title,sizeof(title))){
        ++nav_candidates;
        (void)add_vimm_entry(p,plen,title,VIMM_PAGE,true);
      }
      p=close+4;
    }
  }

  char *row=html;
  while(vimm_count<MAX_VIMM&&(row=strstr(row,"<tr"))){
    ++row_count;
    char *row_tag_end=strchr(row,'>');
    if(!row_tag_end)break;
    char *row_end=strstr(row_tag_end,"</tr>");
    if(!row_end)break;

    char *td=strstr(row_tag_end,"<td");
    if(!td||td>=row_end){row=row_end+5;continue;}
    ++first_td_count;
    char *td_tag_end=strchr(td,'>');
    if(!td_tag_end||td_tag_end>=row_end){row=row_end+5;continue;}
    char *td_end=strstr(td_tag_end,"</td>");
    if(!td_end||td_end>row_end){row=row_end+5;continue;}

    char *anchor=strstr(td_tag_end,"<a");
    if(!anchor||anchor>=td_end){row=row_end+5;continue;}
    ++first_anchor_count;
    char *anchor_tag_end=strchr(anchor,'>');
    if(!anchor_tag_end||anchor_tag_end>=td_end){row=row_end+5;continue;}

    char path[NAME_CAP]={0},title[NAME_CAP]={0};
    const bool have_href=copy_attr_value(anchor,anchor_tag_end,"href",path,sizeof(path));
    char *anchor_end=strstr(anchor_tag_end,"</a>");
    const bool have_title=anchor_end&&anchor_end<=td_end&&
        decode_anchor_text(anchor_tag_end+1,anchor_end,title,sizeof(title));
    const bool numeric=have_href&&vimm_game_path(path,strlen(path));
    if(numeric)++numeric_first_anchor_count;

    if(sampled<12u){
      debug_append("row[%lu] href=%s title=%s numeric=%u\n",
                   (unsigned long)row_count,have_href?path:"<none>",
                   have_title?title:"<none>",numeric?1u:0u);
      ++sampled;
    }
    if(numeric&&have_title)
      (void)add_vimm_entry(path,strlen(path),title,VIMM_GAME,server_filtered);
    row=row_end+5;
  }

  debug_append("parser_summary nav=%lu rows=%lu first_td=%lu first_anchor=%lu numeric_first=%lu added=%lu\n",
    (unsigned long)nav_candidates,(unsigned long)row_count,(unsigned long)first_td_count,
    (unsigned long)first_anchor_count,(unsigned long)numeric_first_anchor_count,
    (unsigned long)vimm_count);
  if(!include_pages&&vimm_count==0)
    copy_text(status_text,sizeof(status_text),"0 games parsed; see Vimm diagnostics");
  debug_flush();
  return true;
}
static bool fetch_vimm(void){
  if(fetch_vimm_url(VIMM_URL,true,false))return true;
  const char fallback[]="https://vimm.net/vault/?p=list&system=GB";
  return fetch_vimm_url(fallback,true,false);
}
static bool append_url_encoded(char *out,size_t cap,const char *text){
  static const char hex[]="0123456789ABCDEF";
  size_t w=0;
  for(size_t i=0;text&&text[i];++i){
    const unsigned char c=(unsigned char)text[i];
    const bool safe=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
                    c=='-'||c=='_'||c=='.'||c=='~';
    if(safe){
      if(w+1u>=cap)return false;
      out[w++]=(char)c;
    }else{
      if(w+3u>=cap)return false;
      out[w++]='%';out[w++]=hex[c>>4];out[w++]=hex[c&15u];
    }
  }
  if(w>=cap)return false;
  out[w]=0;
  return true;
}
static bool search_vimm(const char *query){
  if(!query||!query[0])return false;
  char encoded[240];
  if(!append_url_encoded(encoded,sizeof(encoded),query))return false;
  char url[320];
  int n=snprintf(url,sizeof(url),"https://vimm.net/vault/?p=list&system=GB&q=%s",encoded);
  if(n<=0||(size_t)n>=sizeof(url))return false;
  copy_text(search_query,sizeof(search_query),query);
  return fetch_vimm_url(url,false,true);
}
static bool open_vimm_page(const char *path){
  if(!path||!vimm_page_path(path,strlen(path)))return false;
  const char prefix[]="/vault/GB/";
  const size_t prefix_len=sizeof(prefix)-1u;
  if(strncmp(path,prefix,prefix_len)!=0||!path[prefix_len]||path[prefix_len+1])return false;
  char section=path[prefix_len];
  if(section>='a'&&section<='z')section=(char)(section-'a'+'A');
  if(section<'A'||section>'Z')return false;
  char url[96];
  int n=snprintf(url,sizeof(url),"https://vimm.net/vault/?p=list&system=GB&section=%c",section);
  if(n<=0||(size_t)n>=sizeof(url))return false;
  search_query[0]=0;
  return fetch_vimm_url(url,false,false);
}
static void html_to_detail_text(const char *title){
  size_t w=0; detail_text[0]=0;
  if(title&&title[0]){
    for(size_t i=0;title[i]&&w+1u<DETAIL_CAP;++i)detail_text[w++]=title[i];
    if(w+2u<DETAIL_CAP){detail_text[w++]='\n';detail_text[w++]='\n';}
  }
  bool in_tag=false,skip=false;
  for(const char *p=html;*p&&w+1u<DETAIL_CAP;){
    if(!in_tag&&*p=='<'){
      if(!strncmp(p,"<script",7)||!strncmp(p,"<style",6))skip=true;
      if(!strncmp(p,"</script",8)||!strncmp(p,"</style",7))skip=false;
      if(!strncmp(p,"<br",3)||!strncmp(p,"</p",3)||!strncmp(p,"</div",5)||
         !strncmp(p,"</tr",4)||!strncmp(p,"</h",3)){
        while(w&&detail_text[w-1]==' ')--w;
        if(w&&detail_text[w-1]!='\n')detail_text[w++]='\n';
      }
      in_tag=true;++p;continue;
    }
    if(in_tag){if(*p=='>')in_tag=false;++p;continue;}
    if(skip){++p;continue;}
    if(*p=='&'){
      if(!strncmp(p,"&amp;",5)){detail_text[w++]='&';p+=5;continue;}
      if(!strncmp(p,"&quot;",6)){detail_text[w++]='"';p+=6;continue;}
      if(!strncmp(p,"&#39;",5)){detail_text[w++]='\'';p+=5;continue;}
      if(!strncmp(p,"&nbsp;",6)){if(w&&detail_text[w-1]!=' ')detail_text[w++]=' ';p+=6;continue;}
    }
    char c=*p++; if(c=='\r'||c=='\n'||c=='\t')c=' ';
    if(c==' '&&(!w||detail_text[w-1]==' '||detail_text[w-1]=='\n'))continue;
    detail_text[w++]=c;
  }
  while(w&&(detail_text[w-1]==' '||detail_text[w-1]=='\n'))--w;
  detail_text[w]=0;
}
enum { DETAIL_FAILED=-1, DETAIL_BACK=0, DETAIL_INSTALLED=1 };
static int show_vimm_detail(const char *name,const char *path){
  if(!name||!path||!vimm_game_path(path,strlen(path)))return DETAIL_FAILED;
  char url[NAME_CAP+32u];
  int n=snprintf(url,sizeof(url),"https://vimm.net%s",path);
  if(n<=0||(size_t)n>=sizeof(url))return false;
  const t5_ui_list_row_t loading={name,"Loading Game Boy title details","",0};
  render_rows("Rom Manager","Vimm Vault",&loading,1,0,"");
  if(!fetch_vimm_document(url))return DETAIL_FAILED;
  html_to_detail_text(name);
  if(!detail_text[0])return DETAIL_FAILED;
  int32_t scroll=0;
  for(;;){
    t5_ui_text_view_result_t result={0};
    const t5_ui_chrome_t chrome={"Game Boy title",name,"Vimm Vault","Back","Install ROM","Up","Down"};
    ui->render_text_view(&chrome,detail_text,scroll,&result);
    t5_ui_event_t event={0};
    if(!ui->poll_event(&event,20)||event.type==T5_UI_EVENT_BACK||event.type==T5_UI_EVENT_EXIT)return DETAIL_BACK;
    if(event.type==T5_UI_EVENT_CONFIRM){
      char download_url[512];
      if(!resolve_vimm_download_url(download_url,sizeof(download_url))){
        copy_text(status_text,sizeof(status_text),"No ROM download target found on this title page");
        continue;
      }
      if(import_archive_url(download_url))return DETAIL_INSTALLED;
      continue;
    }else if(event.type==T5_UI_EVENT_PREVIOUS&&scroll<result.max_scroll_lines)++scroll;
    else if(event.type==T5_UI_EVENT_NEXT&&scroll>0)--scroll;
  }
}

static void do_rename(const char *new_text){
  char old_name[NAME_CAP]={0}; size_t n=0;
  if(!new_text||!new_text[0]||!storage->read_file(RENAME_STATE,old_name,sizeof(old_name)-1,&n)||!n||n>=sizeof(old_name)){(void)storage->remove_file(RENAME_STATE);return;}
  old_name[n]=0; (void)storage->remove_file(RENAME_STATE);
  char new_name[NAME_CAP],old_path[PATH_CAP],new_path[PATH_CAP];safe_name(new_text,new_name,sizeof(new_name),"game.gb");ensure_gb_suffix(new_name,sizeof(new_name));make_path(old_name,old_path,sizeof(old_path));make_path(new_name,new_path,sizeof(new_path));
  if(storage->rename_file(old_path,new_path))snprintf(status_text,sizeof(status_text),"Renamed to %.100s",new_name);else copy_text(status_text,sizeof(status_text),"Rename failed or target exists");
}
static view_t consume_keyboard(void){
  char text[384]={0};bool cancelled=false;uint64_t cookie=0;
  if(!system_ui->keyboard_take_result(text,sizeof(text),&cancelled,&cookie))return VIEW_HOME;
  if(cancelled){if(cookie==COOKIE_RENAME)(void)storage->remove_file(RENAME_STATE);return VIEW_HOME;}
  if(cookie==COOKIE_IMPORT){(void)import_url(text);return VIEW_ROMS;}
  if(cookie==COOKIE_SEARCH){if(!search_vimm(text)){if(!status_text[0])copy_text(status_text,sizeof(status_text),"Could not search Vimm Game Boy catalog");return VIEW_HOME;}return VIEW_VIMM;}
  if(cookie==COOKIE_RENAME){do_rename(text);return VIEW_ROMS;}
  return VIEW_HOME;
}
__attribute__((visibility("default"))) void app_main(void){
  app=t5_app_get_api(T5_APP_ABI_VERSION);archive=t5_archive_get_api(T5_ARCHIVE_API_VERSION);storage=t5_storage_get_api(T5_STORAGE_API_VERSION);streams=t5_stream_get_api(T5_STREAM_API_VERSION);system_ui=t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);ui=t5_ui_get_api(T5_UI_API_VERSION);
  const size_t rename_required=offsetof(t5_storage_api_v1,rename_file)+sizeof(storage->rename_file);
  const size_t psram_required=offsetof(t5_app_api_v1,psram_free)+sizeof(app->psram_free);
  if(!app||!archive||!storage||!streams||!system_ui||!ui||
     app->struct_size<psram_required||!app->psram_alloc||!app->psram_free||
     archive->api_version!=T5_ARCHIVE_API_VERSION||archive->struct_size<sizeof(*archive)||
     !archive->find_first_suffix||!archive->extract_file||
     storage->struct_size<rename_required||!storage->exists||!storage->read_file||
     !storage->write_file_atomic||!storage->remove_file||!storage->stream_open||
     !storage->stream_close||!storage->rename_file||
     streams->api_version!=T5_STREAM_API_VERSION||streams->struct_size<sizeof(*streams)||
     !streams->open_file||!streams->open_http||!streams->read||!streams->finish||
     !streams->close||!streams->pipe_connect||!streams->pipe_cancel||!streams->pipe_close||!streams->pipe_info||
     system_ui->api_version!=T5_SYSTEM_UI_API_VERSION||system_ui->struct_size<sizeof(*system_ui)||
     !system_ui->keyboard_request||!system_ui->keyboard_take_result||
     ui->api_version!=T5_UI_API_VERSION||ui->struct_size<sizeof(*ui)||
     !ui->render_list||!ui->render_text_view||!ui->poll_event||!ui->hit_test||!ui->next_index||!ui->previous_index||
     !app->dir_open||!app->dir_next||!app->dir_close||!app->set_back_exits_app||!app->millis)return;
  app->set_back_exits_app(false); status_text[0]=0; search_query[0]=0;
  if(!allocate_workspaces()){
    copy_text(status_text,sizeof(status_text),"PSRAM workspace unavailable");
    const t5_ui_list_row_t row={"Rom Manager cannot start","PSRAM allocation failed","",0};
    render_rows("Rom Manager","Memory",&row,1,0,"");
    t5_ui_event_t event={0};(void)ui->poll_event(&event,1000);
    app->set_back_exits_app(true);return;
  }
  if(!ensure_rom_dir()){copy_text(status_text,sizeof(status_text),"ROM storage unavailable");}
  view_t view=consume_keyboard(); int32_t selected=0;
  for(;;){
    t5_ui_list_row_t rows[MAX_ROMS>MAX_VIMM?MAX_ROMS:MAX_VIMM]; uint32_t count=0; const char *confirm="";
    if(view==VIEW_HOME){rows[0]=(t5_ui_list_row_t){"My ROMs","Rename or delete downloaded .gb files","Open",0};rows[1]=(t5_ui_list_row_t){"Browse Vimm Vault","Browse Game Boy catalog metadata","Browse",0};rows[2]=(t5_ui_list_row_t){"Search Vimm Vault","Filter catalog metadata by title","Search",0};rows[3]=(t5_ui_list_row_t){"Import authorized URL","Download .gb or ZIP and extract first .gb","Import",0};rows[4]=(t5_ui_list_row_t){"Vimm diagnostics","View last fetch/parser diagnostics","Open",0};count=5;confirm="Open";}
    else if(view==VIEW_ROMS){load_roms();for(uint32_t i=0;i<rom_count;++i){static char sizes[MAX_ROMS][24];snprintf(sizes[i],sizeof(sizes[i]),"%llu KB",(unsigned long long)(rom_sizes[i]/1024u));rows[i]=(t5_ui_list_row_t){rom_names[i],"Stored ROM",sizes[i],0};}count=rom_count;confirm=count?"Actions":"";}
    else {for(uint32_t i=0;i<vimm_count;++i)rows[i]=(t5_ui_list_row_t){vimm_names[i],vimm_kinds[i]==VIMM_PAGE?"Game Boy index":"Game Boy title",vimm_kinds[i]==VIMM_PAGE?"Open":"Info",0};count=vimm_count;confirm=count?(vimm_kinds[selected]==VIMM_PAGE?"Open":"Info"):"";}
    render_rows(view==VIEW_HOME?"Rom Manager":view==VIEW_ROMS?"My ROMs":"Vimm Vault",view==VIEW_VIMM?(search_query[0]?search_query:"Game Boy catalog"):ROM_DIR,rows,count,selected,confirm);
    t5_ui_event_t ev={0};if(!ui->poll_event(&ev,20))break;
    if(ev.type==T5_UI_EVENT_EXIT){break;}
    if(ev.type==T5_UI_EVENT_BACK){if(view==VIEW_HOME)break;view=VIEW_HOME;selected=0;continue;}
    if(ev.type==T5_UI_EVENT_PREVIOUS)selected=ui->previous_index(selected,count);
    else if(ev.type==T5_UI_EVENT_NEXT)selected=ui->next_index(selected,count);
    else if(ev.type==T5_UI_EVENT_TAP){int32_t hit=ui->hit_test(ev.touch_x,ev.touch_y);if(hit>=0&&hit<(int32_t)count){if(hit==selected)ev.type=T5_UI_EVENT_CONFIRM;else selected=hit;}}
    if(ev.type!=T5_UI_EVENT_CONFIRM)continue;
    if(view==VIEW_HOME){
      if(selected==0){view=VIEW_ROMS;selected=0;}
      else if(selected==1){search_query[0]=0;copy_text(status_text,sizeof(status_text),"Loading Vimm catalog...");if(fetch_vimm()){view=VIEW_VIMM;selected=0;status_text[0]=0;}else if(!status_text[0])copy_text(status_text,sizeof(status_text),"Could not load Vimm catalog");}
      else if(selected==2){system_ui->keyboard_request("Search Vimm Vault","",79,T5_SYSTEM_KEYBOARD_TEXT,COOKIE_SEARCH);release_workspaces();return;}
      else if(selected==3){system_ui->keyboard_request("Authorized ROM URL","https://",383,T5_SYSTEM_KEYBOARD_URL,COOKIE_IMPORT);release_workspaces();return;}
      else if(selected==4){show_vimm_diagnostics();selected=4;}
    }else if(view==VIEW_VIMM){
      if(vimm_kinds[selected]==VIMM_PAGE){
        copy_text(status_text,sizeof(status_text),"Loading Game Boy titles...");
        if(open_vimm_page(vimm_paths[selected])){selected=0;status_text[0]=0;}
        else copy_text(status_text,sizeof(status_text),"Could not open Game Boy index");
      }else{
        const int detail_result=show_vimm_detail(vimm_names[selected],vimm_paths[selected]);
        if(detail_result==DETAIL_FAILED)
          copy_text(status_text,sizeof(status_text),"Could not load Game Boy title details");
        else if(detail_result==DETAIL_INSTALLED)
          copy_text(status_text,sizeof(status_text),"ROM installed");
        else status_text[0]=0;
      }
    }
    else if(view==VIEW_ROMS&&rom_count){
      char old_path[PATH_CAP];make_path(rom_names[selected],old_path,sizeof(old_path));
      t5_ui_list_row_t actions[3]={{"Rename","Change filename","",0},{"Delete","Remove this ROM","",0},{"Cancel","Return to list","",0}};int32_t a=0;
      for(;;){render_rows("ROM actions",rom_names[selected],actions,3,a,"Select");t5_ui_event_t x={0};if(!ui->poll_event(&x,20)||x.type==T5_UI_EVENT_BACK||x.type==T5_UI_EVENT_EXIT)break;if(x.type==T5_UI_EVENT_PREVIOUS)a=ui->previous_index(a,3);else if(x.type==T5_UI_EVENT_NEXT)a=ui->next_index(a,3);else if(x.type==T5_UI_EVENT_CONFIRM){if(a==0){if(storage->write_file_atomic(RENAME_STATE,rom_names[selected],strlen(rom_names[selected]))){system_ui->keyboard_request("Rename ROM",rom_names[selected],120,T5_SYSTEM_KEYBOARD_TEXT,COOKIE_RENAME);release_workspaces();return;}copy_text(status_text,sizeof(status_text),"Could not stage rename");}if(a==1){if(storage->remove_file(old_path))copy_text(status_text,sizeof(status_text),"ROM deleted");else copy_text(status_text,sizeof(status_text),"Delete failed");load_roms();if(selected>=(int32_t)rom_count)selected=rom_count?(int32_t)rom_count-1:0;}break;}}
    }
  }
  release_workspaces();
  app->set_back_exits_app(true);
}
