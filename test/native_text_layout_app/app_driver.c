#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <T5SerialPortApi.h>
#include <T5StreamApi.h>
#include <T5SystemUiApi.h>
#include <T5UiApi.h>
void app_main(void);
void setup_metrics(void);
void finish_metrics(void);
void measured_text_view(const t5_ui_chrome_t*, const char*, int32_t, t5_ui_text_view_result_t*, bool);
static char input_bytes[11000], captured[12288];
static size_t input_length,input_cursor;
static int after_input, final_renders, releases;
static t5_serial_config_t coding;
static t5_serial_result_t acquire(const t5_serial_port_request_t*r,t5_serial_port_lease_t*l,t5_stream_t*rx,t5_stream_t*tx){coding=r->config;*l=1;*rx=1;*tx=2;return T5_SERIAL_OK;}
static t5_serial_result_t configure(t5_serial_port_lease_t l,const t5_serial_config_t*c){assert(l==1);coding=*c;return T5_SERIAL_OK;}
static t5_serial_result_t status(t5_serial_port_lease_t l,t5_serial_port_state_t*s){assert(l==1);memset(s,0,sizeof(*s));s->config=coding;s->status=T5_SERIAL_STATUS_READY;s->connected=1;s->device=1;strcpy(s->device_label,"Host input fixture");return T5_SERIAL_OK;}
static t5_serial_result_t controls(t5_serial_port_lease_t l,bool d,bool r){assert(l==1);(void)d;(void)r;return T5_SERIAL_OK;}
static t5_serial_result_t release(t5_serial_port_lease_t l){assert(l==1);++releases;return T5_SERIAL_OK;}
static const t5_serial_port_api_v1 serial_api={.api_version=T5_SERIAL_PORT_API_VERSION,.struct_size=sizeof(t5_serial_port_api_v1),.capability_id=T5_SERIAL_PORT_CAPABILITY,.acquire=acquire,.configure=configure,.read_status=status,.set_control_lines=controls,.release=release};
const t5_serial_port_api_v1*t5_serial_port_get_api(uint32_t v){return v==T5_SERIAL_PORT_API_VERSION?&serial_api:NULL;}
static t5_stream_result_t read_stream(t5_stream_t s,void*d,uint32_t cap,uint32_t*n){assert(s==1);size_t left=input_length-input_cursor;*n=left<cap?(uint32_t)left:cap;if(*n){memcpy(d,input_bytes+input_cursor,*n);input_cursor+=*n;return T5_STREAM_OK;}return T5_STREAM_AGAIN;}
static t5_stream_result_t write_stream(t5_stream_t s,const void*d,uint32_t n,uint32_t*out){(void)s;(void)d;(void)n;(void)out;assert(false);return T5_STREAM_IO;}
static const t5_stream_api_v1 stream_api={.api_version=T5_STREAM_API_VERSION,.struct_size=sizeof(t5_stream_api_v1),.read=read_stream,.write=write_stream};
const t5_stream_api_v1*t5_stream_get_api(uint32_t v){return v==T5_STREAM_API_VERSION?&stream_api:NULL;}
static void render_text(const t5_ui_chrome_t*c,const char*s,int32_t scroll,t5_ui_text_view_result_t*r){
 bool final=input_cursor==input_length;
 if(final){if(!final_renders)strcpy(captured,s);else assert(!strcmp(captured,s));++final_renders;}
 measured_text_view(c,s,scroll,r,final);
}
static void render_list(const t5_ui_chrome_t*c,const t5_ui_list_row_t*r,uint32_t n,int32_t s){assert(c&&r&&n);(void)s;}
static int32_t hit(int16_t x,int16_t y){(void)x;(void)y;return T5_UI_HIT_NONE;}
static int32_t next(int32_t n,uint32_t count){return count?(n+1)%(int32_t)count:0;}
static int32_t previous(int32_t n,uint32_t count){return count?(n<=0?(int32_t)count-1:n-1):0;}
static bool poll(t5_ui_event_t*e,uint32_t wait){assert(wait==75);memset(e,0,sizeof(*e));if(input_cursor==input_length){++after_input;if(after_input<=3)e->type=T5_UI_EVENT_PREVIOUS;else if(after_input<=6)e->type=T5_UI_EVENT_NEXT;else e->type=T5_UI_EVENT_BACK;}return true;}
static const t5_ui_api_v1 ui_api={.api_version=T5_UI_API_VERSION,.struct_size=sizeof(t5_ui_api_v1),.render_list=render_list,.hit_test=hit,.poll_event=poll,.next_index=next,.previous_index=previous,.render_text_view=render_text};
const t5_ui_api_v1*t5_ui_get_api(uint32_t v){return v==T5_UI_API_VERSION?&ui_api:NULL;}
static bool request(const char*t,const char*i,size_t m,uint8_t ty,uint64_t c){(void)t;(void)i;(void)m;(void)ty;(void)c;return false;}
static bool take(char*t,size_t n,bool*c,uint64_t*k){(void)t;(void)n;(void)c;(void)k;return false;}
static const t5_system_ui_api_v1 sys_api={.api_version=T5_SYSTEM_UI_API_VERSION,.struct_size=sizeof(t5_system_ui_api_v1),.keyboard_request=request,.keyboard_take_result=take};
const t5_system_ui_api_v1*t5_system_ui_get_api(uint32_t v){return v==T5_SYSTEM_UI_API_VERSION?&sys_api:NULL;}
int main(void){for(size_t i=0;i<192;i++)input_length+=(size_t)snprintf(input_bytes+input_length,sizeof(input_bytes)-input_length,"%04zu: serial data received successfully; status nominal\n",i);assert(input_length==10752);setup_metrics();app_main();assert(releases==1&&final_renders==7&&after_input==7);finish_metrics();puts("APP PASS: unchanged complete Serial Monitor, 42 input chunks, six unchanged-content scroll events, checked exit/release");}
