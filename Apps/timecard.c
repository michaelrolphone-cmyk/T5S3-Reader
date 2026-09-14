#include "T5AppApi.h"
#include "T5StorageApi.h"
#include "T5SystemApi.h"
#include "T5SystemUiApi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAY_COUNT 7
#define PUNCH_COUNT 4
#define WEEK_HISTORY 20
#define WEEK_ITEM_COUNT 11
#define MAX_DAYS 400
#define JSON_CAPACITY 49152
#define STATUS_CAP 128
#define STORE_PATH "/sd/.crosspoint/timecard.json"
#define COOKIE_MAGIC 0x54u

#define SCREEN_WEEK_LIST 0u
#define SCREEN_WEEK 1u
#define SCREEN_DAY 2u
#define TOUCH_NONE 0u
#define TOUCH_HEADER 1u
#define TOUCH_ITEM 2u

typedef struct { int32_t ymd; int16_t punches[PUNCH_COUNT]; } tc_day_t;
typedef struct { int32_t year, month, day; } civil_t;

static const t5_app_api_v1 *app;
static const t5_storage_api_v1 *storage;
static const t5_system_api_v1 *system_api;
static const t5_system_ui_api_v1 *ui;
static tc_day_t days[MAX_DAYS];
static int32_t day_count;
static char json[JSON_CAPACITY];
static uint8_t screen_id;
static int32_t week_offset, selected, editing_ymd;
static char status_text[STATUS_CAP];

static size_t slen(const char *s) { size_t n = 0; if (s) while (s[n]) ++n; return n; }
static void scopy(char *d, size_t c, const char *s) { size_t i=0; if(!d||!c)return; if(!s)s=""; while(s[i]&&i+1<c){d[i]=s[i];++i;} d[i]=0; }
static void sadd(char *d,size_t c,const char*s){size_t n=slen(d),i=0;if(!d||!s||n>=c)return;while(s[i]&&n+1<c)d[n++]=s[i++];d[n]=0;}
static void schar(char*d,size_t c,char ch){size_t n=slen(d);if(!d||n+1>=c)return;d[n]=ch;d[n+1]=0;}
static void suint(char*d,size_t c,uint32_t v){char r[12];size_t n=0;if(!v){schar(d,c,'0');return;}while(v&&n<sizeof(r)){uint32_t q=v/10u;r[n++]=(char)('0'+v-q*10u);v=q;}while(n)schar(d,c,r[--n]);}
static void s2(char*d,size_t c,uint32_t v){schar(d,c,(char)('0'+(v/10u)%10u));schar(d,c,(char)('0'+v%10u));}
static void set_status(const char*s){scopy(status_text,sizeof(status_text),s);}

static civil_t split_ymd(int32_t ymd){civil_t v;v.year=ymd/10000;ymd-=v.year*10000;v.month=ymd/100;v.day=ymd-v.month*100;return v;}
static int32_t ymd(int32_t y,int32_t m,int32_t d){return y*10000+m*100+d;}
static bool leap(int32_t y){if((y&3)!=0)return false;if(y%100!=0)return true;return y%400==0;}
static int32_t mdays(int32_t y,int32_t m){static const uint8_t d[12]={31,28,31,30,31,30,31,31,30,31,30,31};if(m<1||m>12)return 30;if(m==2&&leap(y))return 29;return d[m-1];}
static int32_t serial(int32_t value){civil_t d=split_ymd(value);int32_t n=0,y,m;if(d.year>=1970){for(y=1970;y<d.year;++y)n+=leap(y)?366:365;}else{for(y=1969;y>=d.year;--y)n-=leap(y)?366:365;}for(m=1;m<d.month;++m)n+=mdays(d.year,m);return n+d.day-1;}
static int32_t add_days(int32_t value,int32_t delta){civil_t d=split_ymd(value);while(delta>0){++d.day;if(d.day>mdays(d.year,d.month)){d.day=1;if(++d.month>12){d.month=1;++d.year;}}--delta;}while(delta<0){if(--d.day<1){if(--d.month<1){d.month=12;--d.year;}d.day=mdays(d.year,d.month);}++delta;}return ymd(d.year,d.month,d.day);}
static int32_t weekday(int32_t value){int32_t w=(serial(value)+4)%7;if(w<0)w+=7;return w;}
static int32_t today(void){t5_local_datetime_t n;if(!system_api->local_datetime(&n))return 19700101;return ymd(n.year,n.month,n.day);}
static int32_t now_minutes(void){t5_local_datetime_t n;if(!system_api->local_datetime(&n))return 0;return (int32_t)n.hour*60+n.minute;}
static int32_t sunday(int32_t offset){int32_t t=today();return add_days(t,-weekday(t)+offset*7);}
static int32_t week_number(int32_t value){civil_t d=split_ymd(value);int32_t n=d.day-1;for(int32_t m=1;m<d.month;++m)n+=mdays(d.year,m);return n/7+1;}
static const char* mon(int32_t m){static const char*n[12]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};return(m>=1&&m<=12)?n[m-1]:"?";}
static const char* dow(int32_t value){static const char*n[7]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};return n[weekday(value)];}
static const char* punch_name(uint8_t p){static const char*n[4]={"Clock in","Lunch start","Lunch end","Clock out"};return p<4?n[p]:"Time";}

static tc_day_t blank_day(int32_t value){tc_day_t d;d.ymd=value;for(int i=0;i<4;++i)d.punches[i]=-1;return d;}
static tc_day_t* find_day(int32_t value){for(int32_t i=0;i<day_count;++i)if(days[i].ymd==value)return &days[i];return NULL;}
static tc_day_t get_day(int32_t value){tc_day_t*p=find_day(value);return p?*p:blank_day(value);}
static tc_day_t* ensure_day(int32_t value){tc_day_t*p=find_day(value);if(p)return p;if(day_count>=MAX_DAYS){for(int32_t i=1;i<day_count;++i)days[i-1]=days[i];--day_count;}days[day_count]=blank_day(value);return &days[day_count++];}
static bool any_punch(const tc_day_t*d){if(!d)return false;for(int i=0;i<4;++i)if(d->punches[i]>=0)return true;return false;}

static const char* find_text(const char*b,const char*e,const char*n){size_t l=slen(n);if(!b||!e||!n||!l)return NULL;for(const char*p=b;p+l<=e;++p){size_t i=0;while(i<l&&p[i]==n[i])++i;if(i==l)return p;}return NULL;}
static const char* close_brace(const char*s,const char*e){int depth=0;bool in=false,esc=false;for(const char*p=s;p<e;++p){char ch=*p;if(in){if(esc)esc=false;else if(ch=='\\')esc=true;else if(ch=='"')in=false;continue;}if(ch=='"')in=true;else if(ch=='{')++depth;else if(ch=='}'&&--depth==0)return p;}return NULL;}
static bool int_field(const char*b,const char*e,const char*k,int32_t*out){const char*p=find_text(b,e,k);int32_t v=0;bool neg=false,any=false;if(!p||!out)return false;p+=slen(k);while(p<e&&*p!=':')++p;if(p==e)return false;++p;while(p<e&&(*p==' '||*p=='\n'||*p=='\r'||*p=='\t'))++p;if(p<e&&*p=='-'){neg=true;++p;}while(p<e&&*p>='0'&&*p<='9'){v=v*10+(*p-'0');any=true;++p;}if(!any)return false;*out=neg?-v:v;return true;}
static bool load_store(void){size_t size=0;day_count=0;t5_storage_result_t r=storage->read_file(STORE_PATH,json,sizeof(json)-1,&size);if(r==T5_STORAGE_NOT_FOUND)return true;if(r!=T5_STORAGE_OK||size>=sizeof(json))return false;json[size]=0;const char*end=json+size;const char*p=find_text(json,end,"\"days\"");if(!p)return false;while(p<end&&*p!='[')++p;if(p==end)return false;++p;while(p<end&&*p!=']'&&day_count<MAX_DAYS){while(p<end&&*p!='{'&&*p!=']')++p;if(p>=end||*p==']')break;const char*ce=close_brace(p,end);int32_t date=0;if(!ce||!int_field(p,ce,"\"d\"",&date))return false;if(date>=19700101){tc_day_t d=blank_day(date);int32_t v;if(int_field(p,ce,"\"in\"",&v))d.punches[0]=(int16_t)v;if(int_field(p,ce,"\"ls\"",&v))d.punches[1]=(int16_t)v;if(int_field(p,ce,"\"le\"",&v))d.punches[2]=(int16_t)v;if(int_field(p,ce,"\"out\"",&v))d.punches[3]=(int16_t)v;days[day_count++]=d;}p=ce+1;}return true;}
static bool jchar(size_t*u,char ch){if(!u||*u+1>=sizeof(json))return false;json[(*u)++]=ch;json[*u]=0;return true;}
static bool jtext(size_t*u,const char*s){if(!s)return false;while(*s)if(!jchar(u,*s++))return false;return true;}
static bool jint(size_t*u,int32_t v){char r[16];size_t n=0;uint32_t p;if(v<0){if(!jchar(u,'-'))return false;p=(uint32_t)(-v);}else p=(uint32_t)v;if(!p)return jchar(u,'0');while(p&&n<sizeof(r)){uint32_t q=p/10u;r[n++]=(char)('0'+p-q*10u);p=q;}while(n)if(!jchar(u,r[--n]))return false;return true;}
static bool save_store(void){size_t u=0;bool first=true;if(!jtext(&u,"{\"days\":["))return false;for(int32_t i=0;i<day_count;++i){tc_day_t*d=&days[i];if(!any_punch(d))continue;if(!first&&!jchar(&u,','))return false;first=false;if(!jtext(&u,"{\"d\":" )||!jint(&u,d->ymd))return false;static const char*k[4]={",\"in\":",",\"ls\":",",\"le\":",",\"out\":"};for(int p=0;p<4;++p)if(d->punches[p]>=0){if(!jtext(&u,k[p])||!jint(&u,d->punches[p]))return false;}if(!jchar(&u,'}'))return false;}if(!jtext(&u,"]}"))return false;return storage->write_file_atomic(STORE_PATH,json,u);}
static bool set_punch(int32_t date,uint8_t p,int16_t mins){if(date<19700101||p>=4)return false;if(mins>1439)mins=1439;if(mins<0)mins=-1;tc_day_t*d=ensure_day(date);if(!d)return false;d->punches[p]=mins;return save_store();}

static void format_ampm(int16_t mins,char*out,size_t cap){if(!out||!cap)return;out[0]=0;if(mins<0){sadd(out,cap,"--");return;}int h24=mins/60,m=mins%60,h=h24%12;if(!h)h=12;suint(out,cap,(uint32_t)h);schar(out,cap,':');s2(out,cap,(uint32_t)m);schar(out,cap,' ');sadd(out,cap,h24>=12?"PM":"AM");}
static bool parse_time(const char*text,int16_t*out){const char*p=text;int h=0,m=0,digits=0;bool am=false,pm=false;if(!text||!out)return false;while(*p==' '||*p=='\t')++p;if(!*p){*out=-1;return true;}while(*p>='0'&&*p<='9'){h=h*10+(*p-'0');++p;++digits;}if(!digits)return false;if(*p==':'){digits=0;++p;while(*p>='0'&&*p<='9'&&digits<2){m=m*10+(*p-'0');++p;++digits;}if(!digits)return false;}while(*p){if(*p=='a'||*p=='A')am=true;if(*p=='p'||*p=='P')pm=true;++p;}if(m>59)return false;if(am||pm){if(h<1||h>12)return false;h%=12;if(pm)h+=12;}else if(h>23)return false;*out=(int16_t)(h*60+m);return true;}
static int16_t worked(const tc_day_t*d){if(!d||d->punches[0]<0||d->punches[3]<d->punches[0])return -1;int16_t t=(int16_t)(d->punches[3]-d->punches[0]);if(d->punches[1]>=0&&d->punches[2]>=d->punches[1])t=(int16_t)(t-(d->punches[2]-d->punches[1]));return t<0?0:t;}
static void week_label(int32_t sun,char*out,size_t cap){civil_t a=split_ymd(sun),b=split_ymd(add_days(sun,6));out[0]=0;sadd(out,cap,"Week ");suint(out,cap,(uint32_t)week_number(sun));sadd(out,cap,"  ");sadd(out,cap,mon(a.month));schar(out,cap,' ');suint(out,cap,(uint32_t)a.day);sadd(out,cap," - ");if(a.month!=b.month){sadd(out,cap,mon(b.month));schar(out,cap,' ');}suint(out,cap,(uint32_t)b.day);}
static void punch_status(uint8_t p,int16_t mins){char t[24];status_text[0]=0;sadd(status_text,sizeof(status_text),punch_name(p));sadd(status_text,sizeof(status_text),"  ");format_ampm(mins,t,sizeof(t));sadd(status_text,sizeof(status_text),t);}

static int32_t item_count(void){return screen_id==SCREEN_WEEK_LIST?WEEK_HISTORY:(screen_id==SCREEN_DAY?PUNCH_COUNT:WEEK_ITEM_COUNT);}
static int32_t row_height(void){return screen_id==SCREEN_WEEK_LIST?36:(screen_id==SCREEN_DAY?92:54);}
static int32_t list_top(void){return screen_id==SCREEN_WEEK?142:112;}
static int32_t selected_today(void){int32_t t=today(),s=sunday(0);for(int i=0;i<7;++i)if(add_days(s,i)==t)return i;return 0;}
static void move_selection(int d){int32_t n=item_count();if(!n)return;selected+=d;if(selected<0)selected=n-1;else if(selected>=n)selected=0;}

static void draw_footer(void){int h=app->screen_height();app->draw_text(20,h-56,status_text);app->draw_text(20,h-28,screen_id==SCREEN_WEEK_LIST?"Back: Home  Enter: Open  Up/Down":"Back  Enter: Select  Up/Down");}
static void render_list(void){app->draw_text(20,20,"Time Card");app->draw_text(20,58,"Weeks");for(int i=0;i<WEEK_HISTORY;++i){char l[96]={0},w[64];sadd(l,sizeof(l),i==selected?"> ":"  ");week_label(sunday(-i),w,sizeof(w));sadd(l,sizeof(l),w);if(i==0)sadd(l,sizeof(l),"  This week");app->draw_text(20,list_top()+i*row_height(),l);}}
static void render_week(void){char title[64],v[24];int32_t sun=sunday(week_offset),t=today();app->draw_text(20,20,"Time Card");week_label(sun,title,sizeof(title));app->draw_text(20,58,title);app->draw_text(20,100,"Day");app->draw_text(92,100,"In");app->draw_text(184,100,"Start");app->draw_text(286,100,"End");app->draw_text(382,100,"Out");for(int i=0;i<7;++i){int32_t date=add_days(sun,i);civil_t c=split_ymd(date);tc_day_t d=get_day(date);char label[24]={0};int y=list_top()+i*row_height();sadd(label,sizeof(label),i==selected?">":" ");sadd(label,sizeof(label),dow(date));schar(label,sizeof(label),' ');suint(label,sizeof(label),(uint32_t)c.day);if(date==t)schar(label,sizeof(label),'*');app->draw_text(20,y,label);for(int p=0;p<4;++p){static const int x[4]={92,184,286,382};format_ampm(d.punches[p],v,sizeof(v));app->draw_text(x[p],y,v);}}for(int p=0;p<4;++p){char l[64]={0};int idx=7+p;sadd(l,sizeof(l),idx==selected?"> ":"  ");sadd(l,sizeof(l),punch_name((uint8_t)p));app->draw_text(20,list_top()+idx*row_height(),l);}}
static void render_day(void){civil_t c=split_ymd(editing_ymd);tc_day_t d=get_day(editing_ymd);char sub[48]={0},v[24];app->draw_text(20,20,"Time Card");sadd(sub,sizeof(sub),dow(editing_ymd));schar(sub,sizeof(sub),' ');sadd(sub,sizeof(sub),mon(c.month));schar(sub,sizeof(sub),' ');suint(sub,sizeof(sub),(uint32_t)c.day);app->draw_text(20,58,sub);for(int p=0;p<4;++p){char l[96]={0};sadd(l,sizeof(l),p==selected?"> ":"  ");sadd(l,sizeof(l),punch_name((uint8_t)p));sadd(l,sizeof(l),"    ");format_ampm(d.punches[p],v,sizeof(v));sadd(l,sizeof(l),v);app->draw_text(20,list_top()+p*row_height(),l);}int16_t m=worked(&d);if(m>=0){char total[32]="Worked: ";suint(total,sizeof(total),(uint32_t)(m/60));schar(total,sizeof(total),':');s2(total,sizeof(total),(uint32_t)(m%60));app->draw_text(20,list_top()+4*row_height()+22,total);}}
static void render(void){app->clear();if(screen_id==SCREEN_WEEK_LIST)render_list();else if(screen_id==SCREEN_WEEK)render_week();else render_day();draw_footer();app->present(true);}
static uint8_t hit_test(int16_t y,int32_t*out){int top=list_top(),rh=row_height(),n=item_count(),idx=0;if(!out)return TOUCH_NONE;if(y<88)return screen_id==SCREEN_WEEK_LIST?TOUCH_NONE:TOUCH_HEADER;if(screen_id==SCREEN_WEEK&&y<top)return TOUCH_HEADER;if(y<top)return TOUCH_NONE;int off=y-top;while(off>=rh){off-=rh;++idx;}if(idx<0||idx>=n)return TOUCH_NONE;*out=idx;return TOUCH_ITEM;}

static void open_week(int32_t off){week_offset=off;screen_id=SCREEN_WEEK;selected=off==0?selected_today():0;editing_ymd=0;set_status("Open a day, or punch below");}
static void open_day(int32_t date){tc_day_t d=get_day(date);editing_ymd=date;screen_id=SCREEN_DAY;selected=0;set_status(any_punch(&d)?"Confirm a row to edit":"Confirm a row to set time");}
static void punch_today(uint8_t p){int32_t date=today();int16_t mins=(int16_t)now_minutes();if(!set_punch(date,p,mins)){set_status("Could not save punch");return;}week_offset=0;screen_id=SCREEN_WEEK;editing_ymd=0;selected=selected_today();punch_status(p,mins);}
static uint64_t make_cookie(int32_t date,uint8_t p,int32_t off){uint64_t v=(uint32_t)date;v|=((uint64_t)p)<<32;v|=((uint64_t)(uint16_t)(int16_t)off)<<40;v|=((uint64_t)COOKIE_MAGIC)<<56;return v;}
static bool decode_cookie(uint64_t v,int32_t*date,uint8_t*p,int32_t*off){if((uint8_t)(v>>56)!=COOKIE_MAGIC)return false;if(date)*date=(int32_t)(uint32_t)v;if(p)*p=(uint8_t)(v>>32);if(off)*off=(int32_t)(int16_t)(uint16_t)(v>>40);return true;}
static bool request_edit(void){uint8_t p=(uint8_t)selected;if(screen_id!=SCREEN_DAY||p>=4)return false;tc_day_t d=get_day(editing_ymd);char initial[24];format_ampm(d.punches[p],initial,sizeof(initial));if(d.punches[p]<0)initial[0]=0;if(!ui->keyboard_request(punch_name(p),initial,12,T5_SYSTEM_KEYBOARD_TEXT,make_cookie(editing_ymd,p,week_offset))){set_status("Keyboard unavailable");return false;}return true;}
static bool activate(void){if(screen_id==SCREEN_WEEK_LIST){open_week(-selected);return false;}if(screen_id==SCREEN_DAY)return request_edit();if(selected<7)open_day(add_days(sunday(week_offset),selected));else punch_today((uint8_t)(selected-7));return false;}
static bool consume_keyboard(void){char text[64];bool cancelled=false;uint64_t cookie=0;int32_t date=0,off=0;uint8_t p=0;int16_t mins=-1;if(!ui->keyboard_take_result(text,sizeof(text),&cancelled,&cookie))return false;if(!decode_cookie(cookie,&date,&p,&off)||p>=4||date<19700101){set_status("Keyboard result ignored");return true;}load_store();screen_id=SCREEN_DAY;week_offset=off;selected=p;editing_ymd=date;if(cancelled){tc_day_t d=get_day(date);set_status(any_punch(&d)?"Confirm a row to edit":"Confirm a row to set time");return true;}if(!parse_time(text,&mins)||!set_punch(date,p,mins)){set_status("Edit time");return true;}punch_status(p,mins);return true;}

static bool apis_ok(void){size_t ar=offsetof(t5_app_api_v1,set_back_exits_app)+sizeof(app->set_back_exits_app),sr=offsetof(t5_storage_api_v1,write_file_atomic)+sizeof(storage->write_file_atomic),tr=offsetof(t5_system_api_v1,local_datetime)+sizeof(system_api->local_datetime),ur=offsetof(t5_system_ui_api_v1,navigate_home)+sizeof(ui->navigate_home);return app&&app->abi_version==T5_APP_ABI_VERSION&&app->struct_size>=ar&&app->poll&&app->set_back_exits_app&&storage&&storage->abi_version==T5_STORAGE_ABI_VERSION&&storage->struct_size>=sr&&storage->read_file&&storage->write_file_atomic&&system_api&&system_api->abi_version==T5_SYSTEM_ABI_VERSION&&system_api->struct_size>=tr&&system_api->local_datetime&&ui&&ui->api_version==T5_SYSTEM_UI_API_VERSION&&ui->struct_size>=ur&&ui->keyboard_request&&ui->keyboard_take_result&&ui->navigate_home;}

__attribute__((visibility("default"))) void app_main(void){t5_app_input_t input;bool armed=false;app=t5_app_get_api(T5_APP_ABI_VERSION);storage=t5_storage_get_api(T5_STORAGE_ABI_VERSION);system_api=t5_system_get_api(T5_SYSTEM_ABI_VERSION);ui=t5_system_ui_get_api(T5_SYSTEM_UI_API_VERSION);if(!apis_ok())return;app->set_back_exits_app(false);load_store();if(!consume_keyboard()){screen_id=SCREEN_WEEK_LIST;week_offset=0;selected=0;editing_ymd=0;set_status("Select a week");}render();while(app->poll(&input,20)){uint32_t buttons=input.buttons;if(input.exit_requested)break;if(buttons==0&&!input.tapped){armed=true;continue;}if(!armed)continue;armed=false;if(input.tapped){int32_t touched=selected;uint8_t hit=hit_test(input.touch_y,&touched);if(hit==TOUCH_HEADER){screen_id=SCREEN_WEEK_LIST;selected=-week_offset;if(selected<0)selected=0;if(selected>=WEEK_HISTORY)selected=WEEK_HISTORY-1;editing_ymd=0;set_status("Select a week");render();continue;}if(hit==TOUCH_ITEM){selected=touched;if(activate()){app->set_back_exits_app(true);return;}render();}continue;}if(buttons&T5_APP_BUTTON_BACK){if(screen_id==SCREEN_DAY){screen_id=SCREEN_WEEK;editing_ymd=0;selected=0;set_status("Open a day, or punch below");render();continue;}if(screen_id==SCREEN_WEEK){screen_id=SCREEN_WEEK_LIST;selected=-week_offset;if(selected<0)selected=0;if(selected>=WEEK_HISTORY)selected=WEEK_HISTORY-1;set_status("Select a week");render();continue;}app->set_back_exits_app(true);ui->navigate_home();return;}if(buttons&T5_APP_BUTTON_CONFIRM){if(activate()){app->set_back_exits_app(true);return;}render();continue;}if(buttons&T5_APP_BUTTON_UP){move_selection(-1);render();continue;}if(buttons&T5_APP_BUTTON_DOWN){move_selection(1);render();continue;}}app->set_back_exits_app(true);}
