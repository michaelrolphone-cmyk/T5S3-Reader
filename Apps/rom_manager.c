#include "T5AppApi.h"
#include "T5ArchiveApi.h"
#include "T5StorageApi.h"
#include "T5StreamApi.h"
#include "T5SystemUiApi.h"
#include "T5UiApi.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ROM_DIR "/sd/System/State/Applications/Rom Manager"
#define TMP_ZIP ROM_DIR "/.download.zip"
#define TMP_GB ROM_DIR "/.download.gb"
#define RENAME_STATE ROM_DIR "/.rename.txt"
#define VIMM_URL "https://vimm.net/vault/GB"
#define MAX_ROMS 128u
#define MAX_VIMM 96u
#define NAME_CAP 128u
#define PATH_CAP 384u
#define STATUS_CAP 160u
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
static char html[HTML_CAP + 1u];
static uint32_t html_size;
static char search_query[80];

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
  if(pipe)streams->pipe_close(pipe); streams->close(src); streams->close(dst); if(!ok)storage->remove_file(destination); return ok;
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
static bool fetch_vimm_url(const char *url){
  if(!url||strncmp(url,VIMM_URL,strlen(VIMM_URL))!=0)return false;
  html_size=0; vimm_count=0; t5_stream_t h=0; if(streams->open_http(url,&h)!=T5_STREAM_OK)return false;
  uint32_t last_progress=app->millis(),started=last_progress;
  while(html_size<HTML_CAP){
    uint32_t got=0; int32_t r=streams->read(h,html+html_size,(uint32_t)(HTML_CAP-html_size),&got);
    if(got){html_size+=got;last_progress=app->millis();}
    if(r==T5_STREAM_EOF)break; if(r<0&&r!=T5_STREAM_AGAIN){streams->close(h);return false;}
    t5_ui_event_t ev={0}; if(!ui->poll_event(&ev,5)||ev.type==T5_UI_EVENT_EXIT||ev.type==T5_UI_EVENT_BACK){streams->close(h);return false;}
    uint32_t now=app->millis(); if(now-last_progress>30000u||now-started>60000u){streams->close(h);return false;}
  }
  streams->close(h); html[html_size]=0;
  char *p=html;
  while(vimm_count<MAX_VIMM&&(p=strstr(p,"href=\"/vault/"))){
    p+=6; char *q=strchr(p,'"'); if(!q)break; size_t plen=(size_t)(q-p); if(plen>=NAME_CAP){p=q+1;continue;}
    const uint8_t kind=vimm_game_path(p,plen)?VIMM_GAME:(vimm_page_path(p,plen)?VIMM_PAGE:0u);
    if(!kind){p=q+1;continue;}
    char *gt=strchr(q,'>'); if(!gt)break; char *lt=strchr(gt+1,'<'); if(!lt)break; size_t nlen=(size_t)(lt-(gt+1));
    if(nlen&&nlen<NAME_CAP&&contains_ci(gt+1,search_query)){
      memcpy(vimm_paths[vimm_count],p,plen);vimm_paths[vimm_count][plen]=0;
      memcpy(vimm_names[vimm_count],gt+1,nlen);vimm_names[vimm_count][nlen]=0;
      vimm_kinds[vimm_count]=kind;
      ++vimm_count;
    }
    p=lt+1;
  }
  return true;
}
static bool fetch_vimm(void){return fetch_vimm_url(VIMM_URL);}
static bool open_vimm_page(const char *path){
  if(!path||!vimm_page_path(path,strlen(path)))return false;
  char url[NAME_CAP+32u];
  int n=snprintf(url,sizeof(url),"https://vimm.net%s",path);
  if(n<=0||(size_t)n>=sizeof(url))return false;
  search_query[0]=0;
  return fetch_vimm_url(url);
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
  if(cookie==COOKIE_SEARCH){copy_text(search_query,sizeof(search_query),text);if(!fetch_vimm()){copy_text(status_text,sizeof(status_text),"Could not load Vimm catalog");return VIEW_HOME;}return VIEW_VIMM;}
  if(cookie==COOKIE_RENAME){do_rename(text);return VIEW_ROMS;}
  return VIEW_HOME;
}
__attribute__((visibility("default"))) void app_main(void){
  app=t5_app_get_api(T5_APP_ABI_VERSION);archive=t5_archive_get_api(T5_ARCHIVE_API_VERSION);storage=t5_storage_get_api(T5_STORAGE_API_VERSION);streams=t5_stream_get_api(T5_STREAM_API_VERSION);system_ui=t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);ui=t5_ui_get_api(T5_UI_API_VERSION);
  const size_t rename_required=offsetof(t5_storage_api_v1,rename_file)+sizeof(storage->rename_file);
  if(!app||!archive||!storage||!streams||!system_ui||!ui||
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
     !ui->render_list||!ui->poll_event||!ui->hit_test||!ui->next_index||!ui->previous_index||
     !app->dir_open||!app->dir_next||!app->dir_close||!app->set_back_exits_app||!app->millis)return;
  app->set_back_exits_app(false); status_text[0]=0; search_query[0]=0; if(!ensure_rom_dir()){copy_text(status_text,sizeof(status_text),"ROM storage unavailable");}
  view_t view=consume_keyboard(); int32_t selected=0;
  for(;;){
    t5_ui_list_row_t rows[MAX_ROMS>MAX_VIMM?MAX_ROMS:MAX_VIMM]; uint32_t count=0; const char *confirm="";
    if(view==VIEW_HOME){rows[0]=(t5_ui_list_row_t){"My ROMs","Rename or delete downloaded .gb files","Open",0};rows[1]=(t5_ui_list_row_t){"Browse Vimm Vault","Browse Game Boy catalog metadata","Browse",0};rows[2]=(t5_ui_list_row_t){"Search Vimm Vault","Filter catalog metadata by title","Search",0};rows[3]=(t5_ui_list_row_t){"Import authorized URL","Download .gb or ZIP and extract first .gb","Import",0};count=4;confirm="Open";}
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
      else if(selected==1){search_query[0]=0;copy_text(status_text,sizeof(status_text),"Loading Vimm catalog...");if(fetch_vimm()){view=VIEW_VIMM;selected=0;}else copy_text(status_text,sizeof(status_text),"Could not load Vimm catalog");}
      else if(selected==2){system_ui->keyboard_request("Search Vimm Vault","",79,T5_SYSTEM_KEYBOARD_TEXT,COOKIE_SEARCH);return;}
      else if(selected==3){system_ui->keyboard_request("Authorized ROM URL","https://",383,T5_SYSTEM_KEYBOARD_URL,COOKIE_IMPORT);return;}
    }else if(view==VIEW_VIMM){
      if(vimm_kinds[selected]==VIMM_PAGE){
        copy_text(status_text,sizeof(status_text),"Loading Game Boy titles...");
        if(open_vimm_page(vimm_paths[selected])){selected=0;status_text[0]=0;}
        else copy_text(status_text,sizeof(status_text),"Could not open Game Boy index");
      }else{
        snprintf(status_text,sizeof(status_text),"%.100s: Game Boy title",vimm_names[selected]);
      }
    }
    else if(view==VIEW_ROMS&&rom_count){
      char old_path[PATH_CAP];make_path(rom_names[selected],old_path,sizeof(old_path));
      t5_ui_list_row_t actions[3]={{"Rename","Change filename","",0},{"Delete","Remove this ROM","",0},{"Cancel","Return to list","",0}};int32_t a=0;
      for(;;){render_rows("ROM actions",rom_names[selected],actions,3,a,"Select");t5_ui_event_t x={0};if(!ui->poll_event(&x,20)||x.type==T5_UI_EVENT_BACK||x.type==T5_UI_EVENT_EXIT)break;if(x.type==T5_UI_EVENT_PREVIOUS)a=ui->previous_index(a,3);else if(x.type==T5_UI_EVENT_NEXT)a=ui->next_index(a,3);else if(x.type==T5_UI_EVENT_CONFIRM){if(a==0){if(storage->write_file_atomic(RENAME_STATE,rom_names[selected],strlen(rom_names[selected]))){system_ui->keyboard_request("Rename ROM",rom_names[selected],120,T5_SYSTEM_KEYBOARD_TEXT,COOKIE_RENAME);return;}copy_text(status_text,sizeof(status_text),"Could not stage rename");}if(a==1){if(storage->remove_file(old_path))copy_text(status_text,sizeof(status_text),"ROM deleted");else copy_text(status_text,sizeof(status_text),"Delete failed");load_roms();if(selected>=(int32_t)rom_count)selected=rom_count?(int32_t)rom_count-1:0;}break;}}
    }
  }
  app->set_back_exits_app(true);
}
