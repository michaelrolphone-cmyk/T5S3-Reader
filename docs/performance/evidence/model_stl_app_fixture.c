/* Full-app services are modeled. Parsing, rasterization, and teardown are production. */
static unsigned expected_n, app_presents, display_releases, touch_releases, touch_unsubscribes;
static uint8_t app_pixels[960u * 540u / 8u];
static uint32_t app_now(void) { return (uint32_t)real_clock(); }
static bool app_poll(t5_app_input_t *out,uint32_t wait) {
    memset(out,0,sizeof(*out));
    if(wait==1) { ++parse_polls; return true; }
    assert(wait==8); out->buttons=T5_APP_BUTTON_BACK; return true;
}
static const t5_app_api_v1 full_app={.abi_version=T5_APP_ABI_VERSION,.struct_size=sizeof(t5_app_api_v1),.poll=app_poll,.millis=app_now};
static bool source_path(char*out,size_t capacity){assert(capacity>sizeof("/sd/probe.stl"));strcpy(out,"/sd/probe.stl");return true;}
static const t5_file_open_api_v1 full_file_open={.api_version=T5_FILE_OPEN_API_VERSION,.struct_size=sizeof(t5_file_open_api_v1),.source_path_get=source_path};
static bool display_info(void*c,risc_display_info_v1*out){(void)c;memset(out,0,sizeof(*out));out->api_version=1;out->struct_size=sizeof(*out);out->width=960;out->height=540;out->supported_formats=RISC_DISPLAY_FORMAT_BIT(RISC_DISPLAY_FORMAT_MONO1);return true;}
static bool display_acquire(void*c,uint32_t format,risc_display_surface_v1*out){(void)c;assert(format==RISC_DISPLAY_FORMAT_MONO1);*out=(risc_display_surface_v1){1,app_pixels,960,540,120,sizeof(app_pixels),format};return true;}
static void display_release(void*c,risc_display_frame_v1 frame){(void)c;assert(frame==1);}
static bool display_submit(void*c,risc_display_frame_v1 frame,const risc_display_rect_v1*damage,size_t count,const risc_display_present_options_v1*options,risc_display_present_token_v1*out){(void)c;(void)options;assert(frame==1&&damage&&count==1);*out=++app_presents;return true;}
static bool display_status(void*c,risc_display_present_token_v1 token,risc_display_present_status_v1*out){(void)c;assert(token);memset(out,0,sizeof(*out));out->state=RISC_DISPLAY_PRESENT_COMPLETE;return true;}
static const risc_display_output_api_v1 full_display={.api_version=1,.struct_size=sizeof(risc_display_output_api_v1),.get_info=display_info,.acquire=display_acquire,.release=display_release,.submit=display_submit,.present_status=display_status};
static uint64_t touch_subscribe(void*c){(void)c;return 1;}
static bool touch_unsubscribe(void*c,uint64_t subscription){(void)c;assert(subscription==1);++touch_unsubscribes;return true;}
static bool touch_poll(void*c,size_t max){(void)c;assert(max);return true;}
static int32_t touch_next(void*c,uint64_t subscription,risc_touch_event_v1*out){(void)c;(void)out;assert(subscription==1);return 0;}
static bool touch_snapshot(void*c,risc_touch_snapshot_v1*out){(void)c;memset(out,0,sizeof(*out));return true;}
static const risc_touch_api_v1 full_touch={.api_version=RISC_TOUCH_API_V1,.struct_size=sizeof(risc_touch_api_v1),.subscribe=touch_subscribe,.unsubscribe=touch_unsubscribe,.poll=touch_poll,.next=touch_next,.snapshot=touch_snapshot};
static void verify_mesh(mv_model_t*model,unsigned n){
 assert(model->triangle_count==n&&model->min.x==0&&model->min.y==0&&model->min.z==0&&model->max.x>0&&model->normalize>0);
 for(unsigned i=0;i<n;++i){mv_triangle_t*t=&model->triangles[i];assert(t->a.x==(float)(i%100));assert(t->a.y==(float)((i/100)%100));assert(t->a.z==0);assert(t->b.x==t->a.x+1&&t->b.y==t->a.y&&t->b.z==0);assert(t->c.x==t->a.x&&t->c.y==t->a.y+1&&t->c.z==1);}
}
static bool full_acquire(const char*name,uint32_t version,t5_provider_capability_lease_t*out,const void**api){assert(version==1);*out=0;*api=NULL;if(!strcmp(name,"display.output")){*out=1;*api=&full_display;return true;}if(!strcmp(name,"input.touch.raw")){verify_mesh(&g_model,expected_n);*out=2;*api=&full_touch;return true;}return false;}
static bool full_release(t5_provider_capability_lease_t lease){if(lease==1)++display_releases;else{assert(lease==2);++touch_releases;}return true;}
static const t5_provider_capability_api_v1 full_caps={.api_version=1,.struct_size=sizeof(t5_provider_capability_api_v1),.acquire=full_acquire,.release=full_release};
const t5_provider_capability_api_v1*t5_provider_capability_get_api(uint32_t v){assert(v==1);return &full_caps;}
const t5_math_api_v1*t5_math_get_api(uint32_t v){assert(v==1);return NULL;}
const t5_app_api_v1*t5_app_get_api(uint32_t v){assert(v==T5_APP_ABI_VERSION);return &full_app;}
const t5_storage_api_v1*t5_storage_get_api(uint32_t v){assert(v==1);return &storage_fixture;}
const t5_file_open_api_v1*t5_file_open_get_api(uint32_t v){assert(v==1);return &full_file_open;}
void run_complete_app(unsigned n){
 expected_n=n;parse_polls=0;app_presents=display_releases=touch_releases=touch_unsubscribes=0;
 app_main();assert(app_presents==2&&display_releases==1&&touch_releases==1&&touch_unsubscribes==1);
 assert(!g_model.triangles&&!g_model.triangle_count&&!g_touch_subscription&&!g_touch_lease&&!display_lease);
}
