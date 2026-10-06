/* The authored rail approach, reversible lower bracing and deliberate wool
 * footing test, exercised through real simulation and both raw pad mappings. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail.c"
#include "hollow_trail_route_walk.inc"

static uint32_t now,mapped;
static bool host_exit,poll_failure,snapshot_failure,oversized_snapshot;
static risc_usb_gamepad_state_v1 report;
static unsigned source;
static uint8_t *copy,*bits;
static bool fake_poll(t5_app_input_t *out,uint32_t wait) {
    now+=wait;memset(out,0,sizeof(*out));out->buttons=mapped;out->exit_requested=host_exit;return true;
}
static uint32_t clock_ms(void){return now;}
static const t5_app_api_v1 fake_app={.abi_version=1,.struct_size=sizeof(fake_app),.poll=fake_poll,.millis=clock_ms};
const t5_app_api_v1 *t5_app_get_api(uint32_t v){(void)v;return &fake_app;}
const t5_video_api_v1 *t5_video_get_api(uint32_t v){(void)v;return NULL;}
const t5_math_api_v1 *t5_math_get_api(uint32_t v){(void)v;return NULL;}
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v){(void)v;return NULL;}
static bool raw_poll(void *ctx,size_t n){(void)ctx;(void)n;return !poll_failure;}
static bool snapshot(void *ctx,risc_usb_gamepad_state_v1 *out,size_t *count) {
    (void)ctx;assert(*count);*out=report;*count=oversized_snapshot?5:1;return !snapshot_failure;
}
static const risc_usb_gamepad_api_v1 gamepad={.api_version=1,.struct_size=sizeof(gamepad),.poll=raw_poll,.snapshot=snapshot};
static void input(uint32_t buttons,unsigned hat){report.buttons=buttons;report.hat=(uint8_t)hat;ht_input(1);}
static uint32_t a_button(void){return source?2:1;}
static void press(uint32_t mask){input(0,8);input(mask,8);}
static void action(void){press(a_button());}
static void ticks(unsigned n){for(unsigned i=0;i<n;++i)ht_advance(now+=HT_STEP_MS);}
static void reset(void) {
    memset(&ht,0,sizeof(ht));ht.level=3;ht_select_level(3);ht_spawn(true);
    ht_cabin.active=ht_carriage.active=ht_station.active=false;
    ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=255;
    reading=paused=quitting=loading=debug_jump=debug_select=jump_down=pause_down=false;
    ht_schoolroom_studying=ht_signal_room_studying=false;held=previous=mapped=0;
    ht_pad_owned=ht_pad_fault=ht_input_rearm=false;ht_pad_source=-1;ht_pad_device=0;
    simulation_started=false;simulation_accumulator=0;
    report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
    pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;app=&fake_app;
    host_exit=poll_failure=snapshot_failure=oversized_snapshot=false;
    ht_camera_mode=HT_CAMERA_BASELINE;walk_level=999;
    /* Only controls and ordinary interactions reach the lower-brace entry. */
    for(unsigned n=0;n<16000 && !ht_trestle_near(&ht);++n)walk_route_tick();
    assert(ht.level==3 && !ht.deaths && ht_trestle_near(&ht)==1);
    assert(ht.traversal.mode==HT_FREE && ht.grounded && ht.y==220*256);
    assert(walk_phase==2 && ht_evidence_found(&ht,9));
    input(0,8);
}
static ht_person_pose pose(void) {
    return ht_person_pose_at(ht.x/256-ht.camera/256,ht.y/256-ht.camera_y/256,&ht);
}
static void contact(void) {
    if(ht.traversal.mode!=HT_TRESTLE)return;
    ht_person_pose p=pose();
    for(int i=0;i<2;++i) {
        int dx=p.hand[i].x-p.shoulder.x,dy=p.hand[i].y-p.shoulder.y;
        if(dx*dx+dy*dy>144)fprintf(stderr,"trestle phase=%u facing=%d hand=%d distance2=%d body=%d,%d\n",ht.traversal.trestle_phase,ht.facing,i,dx*dx+dy*dy,ht.x/256,ht.y/256);
        assert(dx*dx+dy*dy<=144);
        dx=p.foot[i].x-p.hip.x;dy=p.foot[i].y-1-p.hip.y;
        if(dx*dx+dy*dy>225)fprintf(stderr,"trestle phase=%u facing=%d foot=%d distance2=%d body=%d,%d\n",ht.traversal.trestle_phase,ht.facing,i,dx*dx+dy*dy,ht.x/256,ht.y/256);
        assert(dx*dx+dy*dy<=225);
        unsigned phase=ht.traversal.trestle_phase;
        if((phase>=16 && phase<80) || (phase>362 && phase<=426)) {
            int hand=p.hand[i].x+ht.camera/256;
            assert(hand==(phase<80?HT_TRESTLE_LEFT+(i?0:14):1474+(i?-6:8)));
            assert((p.foot[i].y+ht.camera_y/256-220)%6==0);
        }
        if(phase>=80 && phase<=362) {
            assert(p.hand[i].y+ht.camera_y/256==HT_TRESTLE_HAND);
            assert(p.foot[i].y+ht.camera_y/256<=HT_TRESTLE_FLOOR);
            assert(p.foot[i].x+ht.camera/256>=HT_TRESTLE_LEFT);
            assert(p.foot[i].x+ht.camera/256<HT_TRESTLE_CAP);
        }
    }
}
static void geometry(void) {
    reset();assert(ht_traversal_interact());assert(ht.traversal.mode==HT_TRESTLE);
    int xs[HT_TRESTLE_STEPS+1],ys[HT_TRESTLE_STEPS+1];
    xs[0]=1234*256;ys[0]=ht.y;contact();
    uint32_t evidence=ht.evidence;ht_puzzle_state puzzle=ht.puzzle;
    for(unsigned n=1;n<=HT_TRESTLE_STEPS;++n) {
        if(ht.traversal.trestle_phase==HT_TRESTLE_TOUCH)assert(ht_traversal_interact());
        ht_step_controls(1,0,false,false);
        assert(ht.traversal.trestle_phase==n);xs[n]=ht.x;ys[n]=ht.y;
        assert(ht_abs(xs[n]-xs[n-1])<=256 && ht_abs(ys[n]-ys[n-1])<=256);contact();
        if(n<HT_TRESTLE_STEPS && n%16==0) {
            ht_step_controls(0,0,true,true);
            assert(ht.x==xs[n] && ht.y==ys[n] && ht.traversal.trestle_phase==n && ht.traversal.mode==HT_TRESTLE);
        }
    }
    assert(ht.traversal.mode==HT_FREE && ht.grounded && ht.x==1490*256 && ht.y==220*256);
    assert(ht.checkpoint==3 && !ht.deaths && ht.evidence==evidence && !memcmp(&puzzle,&ht.puzzle,sizeof(puzzle)));
    assert(ht.traversal.trestle_print_right>ht.traversal.trestle_print_left);
    /* Turn at the real far cap and visit every phase in reverse. */
    ht_step_controls(-1,0,false,false);assert(ht_trestle_near(&ht)==-1);
    assert(ht_traversal_interact());
    for(unsigned n=HT_TRESTLE_STEPS;n>0;--n) {
        ht_step_controls(-1,0,false,false);
        assert(ht.traversal.trestle_phase==n-1 && ht.x==xs[n-1] && ht.y==ys[n-1]);contact();
    }
    assert(ht.traversal.mode==HT_FREE && ht.grounded && !ht.deaths);
}
static void fixed_attachment(const ht_game *saved) {
    assert(ht.traversal.mode==HT_TRESTLE);
    if(ht.traversal.trestle_phase!=saved->traversal.trestle_phase)fprintf(stderr,"attachment source=%u phase=%u expected=%u\n",source,ht.traversal.trestle_phase,saved->traversal.trestle_phase);
    assert(ht.traversal.trestle_phase==saved->traversal.trestle_phase);
    assert(ht.x==saved->x && ht.y==saved->y && !ht.vx && !ht.vy);
    assert(ht.traversal.trestle_tested==saved->traversal.trestle_tested);
    assert(ht.evidence==saved->evidence && !memcmp(&ht.puzzle,&saved->puzzle,sizeof(ht.puzzle)));
}
static void to_phase(unsigned target) {
    input(0,2);
    for(unsigned n=0;n<=HT_TRESTLE_STEPS && ht.traversal.trestle_phase<target;++n)ticks(1);
    input(0,8);assert(ht.traversal.trestle_phase==target);
}
static void controls(void) {
    for(source=0;source<2;++source) {
        reset();action();assert(ht.traversal.mode==HT_TRESTLE && !reading);
        /* Holding the entry A all the way to the wool is not a fresh test. */
        input(a_button(),2);ticks(HT_TRESTLE_STEPS);assert(ht.traversal.trestle_phase==HT_TRESTLE_TOUCH && !ht.traversal.trestle_tested);
        ht_game stop=ht;ticks(120);fixed_attachment(&stop);
        input(0,8);ticks(60);fixed_attachment(&stop);
        press(source?1:2);ticks(2);fixed_attachment(&stop); /* B never detaches. */
        press(source?64:256);ticks(1);assert(paused);ht_game frozen=ht;
        ticks(80);assert(!memcmp(&ht,&frozen,sizeof(ht)));
        press(source?64:256);ticks(1);assert(!paused);fixed_attachment(&stop);
        press(source?128:512);assert(reading);frozen=ht;ticks(80);assert(!memcmp(&ht,&frozen,sizeof(ht)));
        press(source?4:8);assert(!reading);input(0,8);fixed_attachment(&stop);
        action();assert(ht.traversal.trestle_tested);input(0,8);
        int body=ht.x;ht_person_pose before=pose();int before_foot=before.foot[0].x+ht.camera/256;to_phase(HT_TRESTLE_TOUCH+32);
        ht_person_pose tested=pose();assert(ht.x==body);
        assert(tested.foot[0].x+ht.camera/256>before_foot);
        assert(tested.foot[0].y+ht.camera_y/256==HT_TRESTLE_FLOOR);
        to_phase(HT_TRESTLE_TOUCH+48);assert(ht.x==body);to_phase(HT_TRESTLE_TEST_END-1);assert(ht.x>body);
        /* Each provider failure suppresses stale movement and mapped A,
         * even when its report arrives at the next simulation deadline. */
        for(unsigned fault=0;fault<4;++fault) {
            input(0,2);ticks(2);frozen=ht;now+=HT_STEP_MS-1;
            mapped=T5_APP_BUTTON_CONFIRM|T5_APP_BUTTON_RIGHT;
            poll_failure=fault==0;snapshot_failure=fault==1;oversized_snapshot=fault==2;
            if(fault==3)report.connected=0;
            input(a_button(),2);ticks(8);fixed_attachment(&frozen);assert(ht_input_rearm);
            poll_failure=snapshot_failure=oversized_snapshot=false;report.connected=1;mapped=0;
            input(a_button(),2);ticks(8);fixed_attachment(&frozen);assert(ht_input_rearm);
            input(0,8);assert(!ht_input_rearm);input(0,2);ticks(1);assert(ht.traversal.trestle_phase>frozen.traversal.trestle_phase);
            input(0,8);
        }
        /* A new raw device needs neutral before accepting held input. */
        input(0,2);ticks(1);frozen=ht;now+=HT_STEP_MS-1;
        ++report.device;input(a_button(),2);ticks(4);fixed_attachment(&frozen);
        input(0,8);assert(!ht_input_rearm);
        /* A neutral provider switch must discard the previous held report
         * before that neutral report can clear the new ownership gate. */
        input(0,2);ticks(1);frozen=ht;now+=HT_STEP_MS-1;
        pad=source?&gamepad:NULL;hid_pad=source?NULL:&gamepad;
        input(0,8);ticks(4);fixed_attachment(&frozen);assert(!ht_input_rearm);
        host_exit=true;input(0,8);assert(quitting);
    }
}
static void lower_attachment_sweep(void) {
    source=0;reset();ht_game entry=ht;
    /* Isolated valid grounded states enumerate the complete lower member;
     * the separate recovery witness reaches it using only actual controls. */
    for(int x=1245;x<=1475;++x) {
        ht=entry;ht.x=x*256;ht.y=HT_TRESTLE_FLOOR*256;ht.vx=ht.vy=0;ht.grounded=true;
        assert(ht_trestle_near(&ht)==2 && ht_traversal_interact());ht_step_controls(0,0,false,false);
        if(ht_abs(ht.x-x*256)>256 || ht_abs(ht.y-HT_TRESTLE_FLOOR*256)>256)fprintf(stderr,"lower reattach x=%d gives=%d,%d phase=%u\n",x,ht.x/256,ht.y/256,ht.traversal.trestle_phase);
        assert(ht_abs(ht.x-x*256)<=256 && ht_abs(ht.y-HT_TRESTLE_FLOOR*256)<=256);contact();
    }
}
static void recovery(void) {
    source=0;reset();
    /* Walking off the approach falls onto the physical lower member. */
    input(0,2);for(unsigned n=0;n<100 && !(ht.grounded && ht.y==HT_TRESTLE_FLOOR*256);++n)ticks(1);
    input(0,8);assert(ht.grounded && ht.y==HT_TRESTLE_FLOOR*256 && ht.traversal.mode==HT_FREE && !ht.deaths);
    int x=ht.x;action();assert(ht.traversal.mode==HT_TRESTLE);ticks(1);assert(ht_abs(ht.x-x)<=256);
    input(0,2);for(unsigned n=0;n<HT_TRESTLE_STEPS && ht.traversal.trestle_phase<HT_TRESTLE_TOUCH;++n)ticks(1);
    input(0,8);assert(ht.traversal.trestle_phase==HT_TRESTLE_TOUCH);action();
    input(0,2);for(unsigned n=0;n<HT_TRESTLE_STEPS && ht.traversal.mode==HT_TRESTLE;++n)ticks(1);
    input(0,8);assert(ht.traversal.mode==HT_FREE && ht.grounded && ht.checkpoint==3 && !ht.deaths);
    /* A recovers ordinary lower-beam walking onto the far-cap climb. */
    reset();input(0,2);for(unsigned n=0;n<400 && ht.x<1470*256;++n)ticks(1);input(0,8);
    assert(ht.traversal.mode==HT_FREE && ht.grounded && ht.y==HT_TRESTLE_FLOOR*256 && ht.x>=1470*256 && ht.x<=1475*256 && !ht.deaths);
    action();input(0,2);for(unsigned n=0;n<HT_TRESTLE_STEPS && ht.traversal.mode==HT_TRESTLE;++n)ticks(1);
    input(0,8);assert(ht.grounded && ht.x==1490*256 && ht.y==220*256 && !ht.deaths);
    /* Retry uses the real lower checkpoint, and can reattach there. */
    uint32_t evidence=ht.evidence;ht_spawn(false);assert(ht.traversal.mode==HT_FREE && ht.y==HT_TRESTLE_FLOOR*256 && ht.evidence==evidence);
    input(0,8);action();assert(ht.traversal.mode==HT_TRESTLE);
    input(0,2);for(unsigned n=0;n<HT_TRESTLE_STEPS && ht.traversal.mode==HT_TRESTLE;++n) {
        if(ht.traversal.trestle_phase==HT_TRESTLE_TOUCH && !ht.traversal.trestle_tested){action();input(0,2);}
        ticks(1);
    }
    input(0,8);assert(ht.x==1490*256 && ht.grounded && !ht.deaths);
    /* Continue the same actual route over its rope gap, evidence and puzzle. */
    walk_phase=3;walk_task=0;
    for(unsigned n=0;n<16000 && ht.level==3;++n)walk_route_tick();
    assert(ht.level==4 && !ht.deaths && (walk_mechanics&8));
    assert(ht_evidence_found(&ht,9) && ht_evidence_found(&ht,10) && ht_evidence_found(&ht,11));
}
static unsigned ink_in(int x0,int y0,int x1,int y1) {
    unsigned ink=0;for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)ink+=(bits[y*120+x/8]>>(7-(x&7)))&1u;return ink;
}
static void render(void) {
    source=0;reset();assert(ht_traversal_interact());
    for(unsigned n=0;n<HT_TRESTLE_TOUCH;++n)ht_step_controls(1,0,false,false);
    ht.camera=1140*256;ht.camera_y=80*256;
    for(unsigned native=0;native<2;++native) {
        ht_native_active=native!=0;ht_native_foreground_half_y=false;ht_world_scale=256;ht_scene=native?ht_native_a:ht_scene_low;
        int bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;ht_game frozen=ht;
        memset(ht_scene,0,bytes);ht_trestle_structure(&ht);ht_trestle_member(&ht,0);ht_trestle_member(&ht,1);
        memcpy(copy,ht_scene,bytes);ht_pack_mono(bits,120);assert(!memcmp(&ht,&frozen,sizeof(ht)));
        memset(ht_scene,0,bytes);ht_trestle_structure(&ht);ht_trestle_member(&ht,0);ht_trestle_member(&ht,1);
        assert(!memcmp(copy,ht_scene,bytes) && !memcmp(&ht,&frozen,sizeof(ht)));
        /* The hanging wool and solid hand/foot members survive mono output. */
        int scale=2,wx=(HT_TRESTLE_WOOL-1140)*scale,wy=(HT_TRESTLE_HAND-80)*scale;
        unsigned area=(unsigned)((10*scale)*(12*scale));
        unsigned wool=ink_in(wx-2*scale,wy+4*scale,wx+8*scale,wy+16*scale);
        assert(wool>0 && wool<area);
        assert(ink_in((1280-1140)*scale,(HT_TRESTLE_FLOOR-80)*scale,(1300-1140)*scale,(HT_TRESTLE_FLOOR-80+3)*scale)>0);
        assert(ink_in((1280-1140)*scale,(HT_TRESTLE_HAND-80)*scale,(1300-1140)*scale,(HT_TRESTLE_HAND-80+3)*scale)>0);
        /* Neither support nor artwork silently bridges the required rope gap. */
        int l,r,top;assert(ht_platform_piece(&ht,3,0,&l,&r,&top) && r==HT_TRESTLE_CAP && top==HT_TRESTLE_FLOOR);
        assert(ht_platform_piece(&ht,3,1,&l,&r,&top) && l==HT_TRESTLE_CAP && r==HT_TRESTLE_END && top==220);
        assert(!ht_platform_piece(&ht,3,2,&l,&r,&top));
    }
    ht_native_active=false;ht_scene=ht_scene_low;
    for(unsigned native=0;native<2;++native) {
        ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
        ht_render_scene_from(&ht,true);int bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;memcpy(copy,ht_scene,bytes);ht_pack_mono(bits,120);
        ht_render_scene_from(&ht,true);assert(!memcmp(copy,ht_scene,bytes) && !memcmp(&ht,&frozen,sizeof(ht)));
    }
}
int main(int argc,char **argv) {
    uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY);copy=malloc(HT_NATIVE_PIXELS);bits=malloc(HT_NATIVE_PIXELS/8);
    assert(memory&&copy&&bits);ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bits;
    if(argc==1 || !strcmp(argv[1],"geometry"))geometry();
    if(argc==1 || !strcmp(argv[1],"controls"))controls();
    if(argc==1 || !strcmp(argv[1],"recovery")){lower_attachment_sweep();recovery();}
    if(argc==1 || !strcmp(argv[1],"render"))render();
    free(bits);free(copy);free(memory);
    puts("Trestle: real rail approach, reversible bounded contacts, fresh wool test, HID/XInput interruptions, lower-beam recovery, rope route and mono rendering PASS");
}
