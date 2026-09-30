/* Host gameplay and memory-safety smoke test; no firmware/hardware mock. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../../Apps/hollow_trail_engine.inc"
#include "hollow_trail_route_walk.inc"

static uint32_t checksum(const uint8_t *data,size_t size) {
    uint32_t h=2166136261u;
    for(size_t i=0;i<size;++i) h=(h^data[i])*16777619u;
    return h;
}
static void mechanism_invariants(void) {
    ht_puzzle_state p;ht_puzzle_reset(&p,0);ht_puzzle_operate(&p,0,0);
    assert(p.value[0]==1 && p.value[1]==1 && !p.value[2]);
    /* Fuel, weights and power cannot be created or destroyed by experimentation. */
    const unsigned conserved[]={2,5,7};
    for(unsigned i=0;i<3;++i) {
        unsigned level=conserved[i];ht_puzzle_reset(&p,level);
        for(int k=0;k<60;++k) {
            ht_puzzle_operate(&p,level,(k*k+k/3)%3);
            assert(p.value[0]+p.value[1]+p.value[2]==(level==2?8:6));
            if(level==2) assert(p.value[0]<=8 && p.value[1]<=5 && p.value[2]<=3);
        }
    }
    /* A full spur cannot swallow another wagon; selection stays available
     * for a different destination or a same-road cancellation. */
    ht_puzzle_reset(&p,3);ht_puzzle_operate(&p,3,0);ht_puzzle_operate(&p,3,2);
    ht_puzzle_operate(&p,3,0);ht_puzzle_operate(&p,3,2);
    assert(p.wrong && p.feedback==HT_SIDING_FULL && p.count[0]==2 && p.count[2]==1 && p.selected==0);
    ht_puzzle_operate(&p,3,0);assert(p.selected==3 && !p.solved);
    /* Four measures, two chambers; gates cannot equalize pressure by magic. */
    ht_puzzle_reset(&p,4);ht_puzzle_operate(&p,4,3);assert(p.feedback==HT_OPEN_GATE && p.value[0]==0);
    ht_puzzle_operate(&p,4,1);assert(p.feedback==HT_PRESSURE && !p.stage);
    ht_puzzle_operate(&p,4,0);ht_puzzle_operate(&p,4,3);ht_puzzle_operate(&p,4,3);
    assert(p.value[0]==2 && p.value[1]==2);
    ht_puzzle_operate(&p,4,1);assert(p.stage==1 && p.value[2]==2);
    ht_puzzle_operate(&p,4,3);assert(p.feedback==HT_OPEN_GATE && p.value[0]+p.value[1]==4);
    /* A correct-looking beam can reach the lower glass before it is cleared. */
    ht_puzzle_reset(&p,6);ht_puzzle_operate(&p,6,0);ht_puzzle_operate(&p,6,1);
    ht_puzzle_operate(&p,6,2);ht_puzzle_operate(&p,6,2);ht_puzzle_operate(&p,6,3);
    assert(ht_mirror_lit(&p) && !p.solved && !p.stage && p.feedback==HT_GLASS_FOGGED);
    ht_puzzle_operate(&p,6,2);ht_puzzle_operate(&p,6,3);assert(p.stage && !p.solved);
    /* Running allocation does not bypass a cold return's priming requirement. */
    ht_puzzle_reset(&p,7);
    for(int i=0;i<4;++i) ht_puzzle_operate(&p,7,0);
    for(int i=0;i<3;++i) ht_puzzle_operate(&p,7,1);
    ht_puzzle_operate(&p,7,3);assert(!p.solved && !p.stage && p.feedback==HT_PRIME_FAILED);
    /* The burnt crossover rejects an otherwise correct contact permutation. */
    ht_puzzle_reset(&p,1);const char *shorted="01123";
    for(const char *c=shorted;*c;++c) ht_puzzle_operate(&p,1,*c-'0');
    assert(!p.solved && p.feedback==HT_CROSSOVER_SHORT);
    ht_puzzle_reset(&p,8);ht_puzzle_operate(&p,8,1);
    assert(!p.progress && p.feedback==HT_ECHO_CONNECTED);
    for(unsigned code=0;code<=HT_DECOUPLED;++code) {p.feedback=(uint8_t)code;assert(strlen(ht_puzzle_feedback(&p))*6<=348);}
}
/* Reachable-state liveness: every legal experiment can still reach a
 * solution. Fixed small test graph, never shipped in the app. */
static void mechanism_recovery(void) {
    static ht_puzzle_state states[256];
    static uint16_t edges[256][4];
    static bool wins[256];
    for(unsigned level=0;level<HT_LEVELS;++level) {
        unsigned count=1;ht_puzzle_reset(&states[0],level);
        memset(wins,0,sizeof(wins));
        for(unsigned node=0;node<count;++node) {
            wins[node]=states[node].solved;
            for(int control=0;control<ht_puzzle_controls(level);++control) {
                ht_puzzle_state next=states[node];ht_puzzle_operate(&next,level,control);
                next.feedback=0;next.wrong=false;
                unsigned found=0;while(found<count && memcmp(&states[found],&next,sizeof(next))) ++found;
                if(found==count) {assert(count<256);states[count++]=next;}
                edges[node][control]=(uint16_t)found;
            }
        }
        for(unsigned pass=0;pass<count;++pass) {
            bool changed=false;
            for(unsigned node=0;node<count;++node) if(!wins[node]) {
                for(int control=0;control<ht_puzzle_controls(level);++control)
                    if(wins[edges[node][control]]) {wins[node]=true;changed=true;break;}
            }
            if(!changed) break;
        }
        for(unsigned node=0;node<count;++node) if(!wins[node]) {
            fprintf(stderr,"Unrecoverable chapter %u: values %u/%u/%u stage %u selected %u\n",level,
                states[node].value[0],states[node].value[1],states[node].value[2],states[node].stage,states[node].selected);
            assert(wins[node]);
        }
    }
}
static void camera_invariants(void) {
    ht.level=0;ht_spawn(true);ht.camera_mood=0;ht.rotation_phase=256u<<8;ht.sway_phase=1024;
    int quiet,scale,panic,zoom;ht_camera_coefficients(&ht,&quiet,&scale);
    ht.camera_mood=256;ht_camera_coefficients(&ht,&panic,&zoom);
    assert(quiet>=56 && quiet<=58 && panic==344);
    ht.camera_mood=0;ht.rotation_phase=0;ht_camera_coefficients(&ht,&quiet,&scale);
    assert(scale==3414); /* Existing breathing peak is unchanged. */
    ht.drop_zoom=256;ht_camera_coefficients(&ht,&quiet,&zoom);assert(zoom>scale && zoom==4096);
    ht_spawn(true);ht.x=0;ht_camera_step(true);assert(ht.rotation_phase==213 && ht.sway_phase==5);
    unsigned rotation=ht.rotation_phase,breathing=ht.sway_phase;
    ht_camera_step(false);assert(ht.rotation_phase==rotation && ht.sway_phase==breathing);
    ht.y=ht.airborne_origin+60*256;ht.vy=1000;ht.grounded=false;
    for(int i=0;i<16;++i) ht_camera_step(false);
    assert(ht.drop_zoom==256 && ht.rotation_phase==rotation && ht.sway_phase==breathing);
    ht.grounded=true;for(int i=0;i<64;++i) ht_camera_step(false);assert(!ht.drop_zoom);
    ht.level=7;ht_spawn(true);ht.x=1900*256;ht.scene_evidence=2;ht.weather_amount=256;
    assert(ht_mood_target(&ht)==256);
    ht.camera_mood=256;ht.rotation_phase=0;ht_camera_step(true);assert(ht.rotation_phase==1280);
    ht.level=0;ht_spawn(true);
}
/* Distant silhouettes must survive the sky floor and off-focus fog. This
 * catches the regression where a low pale replacement erased the far plane. */
static void visible_background_depth(void) {
    for(unsigned level=0;level<2;++level) {
        ht.level=level;ht_spawn(true);ht_select_level(level);
        for(int camera=0;camera<=1400;camera+=700) {
            ht_clear_layer(ht_raw);ht_background(0,ht_layer_offset(0,camera));
            unsigned visible=0;
            for(int y=20;y<130;++y) for(int x=20;x<HT_W-20;++x)
                if(ht_raw[y*HT_W+x]*211/256>ht_sky_ink(&ht)+25) ++visible;
            assert(visible>(level?4000u:700u));
        }
    }
    ht.level=0;ht_spawn(true);ht_select_level(0);
}
static void landscape_contact_and_pull(void) {
    ht.level=0;ht_spawn(true);ht.camera=670*256;ht.camera_y=40*256;
    /* Root toes and trunk contact continuous soil on both slopes, including
     * the widest vista. Inspect isolated tree pixels, not an already black floor. */
    const int positions[]={760,900,1020};
    for(int scale=176;scale<=256;scale+=40) for(unsigned i=0;i<3;++i) {
        int world=positions[i];ht_world_scale=scale;
        memset(ht_scene,0,HT_PIXELS);ht_grounded_tree(&ht,2,world,340,23,901);
        for(int dx=-65;dx<=43;dx+=3) {
            int px=ht_project_x(world+dx-670);
            int py=ht_project_y(ht_surface_at(&ht,2,world+dx)-40);
            assert(px>=0 && px<HT_W && py>=0 && py<HT_H);
            assert(ht_scene[py*HT_W+px]==255);
        }
    }
    /* Player separation follows projection and never erases sloping soil. */
    ht.x=900*256;ht.y=ht_surface_at(&ht,2,900)*256;
    for(int scale=176;scale<=256;scale+=40) {
        ht_world_scale=scale;memset(ht_scene,255,HT_PIXELS);
        ht_character(230,ht.y/256-40,&ht);
        for(int wx=876;wx<=924;++wx) {
            int px=ht_project_x(wx-670),py=ht_project_y(ht_surface_at(&ht,2,wx)-40)+2;
            assert(ht_scene[py*HT_W+px]==255);
        }
    }
    ht_world_scale=256;
    /* Parcel joins cannot reset a continuous hillside to the old flat datum. */
    for(int i=2;i<9;++i) if(ht_land[i].right==ht_land[i+1].left) {
        int edge=ht_land[i].right;
        assert(ht_abs(ht_surface_at(&ht,i,edge-1)-ht_surface_at(&ht,i+1,edge))<=1);
    }
    uint8_t pushed[HT_PIXELS];
    ht.camera=ht.camera_y=0;ht.x=200*256;ht.y=220*256;
    for(int mode=HT_ROLL;mode<=HT_CRATE;++mode) for(int side=-1;side<=1;side+=2) {
        ht.traversal.mode=mode;ht.traversal.ball_x=ht.traversal.crate_x=(200+side*21)*256;
        ht.traversal.ball_y=204*256;ht.traversal.crate_y=220*256;
        ht.traversal.ball_vx=ht.traversal.crate_vx=side*128;
        memset(ht_scene,0,HT_PIXELS);assert(ht_character_instrument(200,220,&ht));
        memcpy(pushed,ht_scene,HT_PIXELS);
        ht.traversal.ball_vx=ht.traversal.crate_vx=-side*128;
        memset(ht_scene,0,HT_PIXELS);assert(ht_character_instrument(200,220,&ht));
        assert(memcmp(pushed,ht_scene,HT_PIXELS));
        int hand=221; /* Object-facing hand remains on the same load contact. */
        if(side<0) hand=179;
        hand-=side*(mode==HT_ROLL?HT_BALL_RADIUS-2:HT_CRATE_HALF);
        int hy=mode==HT_ROLL?200:202;
        assert(ht_scene[hy*HT_W+hand] && pushed[hy*HT_W+hand]);
    }
    ht_spawn(true);
}
static void eroded_cliffs_and_grotto(void) {
    ht.level=0;ht_spawn(true);ht.camera=1250*256;ht.camera_y=200*256;
    int i=4,right=ht_land[i].right;
    assert(ht_cliff_edge(&ht,i,1));
    memset(ht_scene,0,HT_PIXELS);
    ht_draw_land(&ht,i,0,ht_land[i].left,right,ht_land[i].top);
    for(int y=260;y<360;y+=7) {
        int edge=right-ht_cliff_inset(&ht,i,1,y);
        assert(edge<right);
        assert(ht_scene[(y-200)*HT_W+edge-1250-2]>=230);
        assert(ht_scene[(y-200)*HT_W+edge-1250+2]==0);
    }
    /* Approach the eroded side below the lip; body contact must match the
     * outermost rock across its full height, not the old rectangular edge. */
    int left=ht_land[i].left,wall=right;
    ht_cliff_bounds(&ht,i,291,320,&left,&wall);
    ht.y=320*256;ht.x=(wall+2)*256;ht.vx=-640;ht.vy=0;ht.grounded=false;
    ht_land_collision(i,(right+12)*256,ht.y);
    assert(ht.x==(wall+5)*256 && !ht.vx && !ht.grounded);
    int bx=1464*256,by=300*256,bvx=-128,bvy=0;
    ht_body_step(&bx,&by,&bvx,&bvy,HT_BALL_RADIUS,0,true);
    assert(bx==1464*256-128); /* No snap toward still-distant eroded rock. */
    /* Jumping in the low entrance touches the visible grotto ceiling, then
     * falls back to its unchanged floor. It never clips through the roof. */
    ht.level=4;ht_spawn(true);
    for(int tick=0;tick<50;++tick) {
        ht_step_controls(0,0,tick==0,true);
        assert(ht.y-29*256>=ht_grotto_ceiling(ht.x/256)*256);
    }
    assert(ht.grounded);
    /* Visual water extends under the eroded bank, across both physics bounds.
     * The collision channel must not become a rectangular paint clip. */
    ht.camera=120*256;ht.camera_y=40*256;ht_world_scale=256;
    memset(ht_scene,0,HT_PIXELS);ht_boat_grotto(&ht);
    for(int edge=350;edge<=680;edge+=330) {
        int px=edge-120,py=240;
        if(px>1 && px<HT_W-1) {
            assert(ht_scene[py*HT_W+px-1]>20);
            assert(ht_abs(ht_scene[py*HT_W+px-1]-ht_scene[py*HT_W+px+1])<8);
        }
    }
    assert(ht_scene[110*HT_W+130]>ht_scene[110*HT_W+420]+40);
    /* Bright opening must end at readable water, not wash through it. */
    assert(ht_scene[234*HT_W+440]>ht_scene[224*HT_W+440]+45);
    assert(ht_scene[242*HT_W+278]>ht_scene[242*HT_W+410]+25);
    assert(ht_boat_half(&ht)==48);
    assert(ht_mech(&ht)->boat_left-ht_boat_half(&ht)==ht_mech(&ht)->water_left);
    assert(ht_mech(&ht)->boat_right+ht_boat_half(&ht)==ht_mech(&ht)->water_right);
    ht.level=0;ht_spawn(true);
}
static void terrain_and_rope_invariants(void) {
    ht.level=0;ht_spawn(true);ht.x=710*256;ht.y=ht_surface_at(&ht,2,710)*256;
    int high=ht.y,low=ht.y;
    for(int direction=1;direction>=-1;direction-=2) for(int step=0;step<130;++step) {
        ht_step_controls(direction,0,false,false);
        assert(ht.grounded && ht.y==ht_surface_at(&ht,2,ht.x/256)*256);
        high=ht_min(high,ht.y);low=ht_max(low,ht.y);
    }
    assert(low-high>=30*256); /* Actually walk a rounded hill without jumping. */
    assert(ht_terrain_bottom(&ht,2,875)==520);
    assert(ht_terrain_bottom(&ht,1,520)==ht_surface_at(&ht,1,520)+18);
    ht.camera=670*256;ht.camera_y=0;memset(ht_scene,0,HT_PIXELS);
    ht_draw_land(&ht,2,0,670,1080,220);
    for(int x=700;x<1060;x+=13) {
        int top=ht_surface_at(&ht,2,x);
        assert(ht_scene[top*HT_W+x-670]>=230);
        assert(ht_scene[(top+30)*HT_W+x-670]==255);
        assert(ht_weather_floor(&ht,x-670)==top);
    }
    /* Catch six different heights, visibly descend over time, and jump
     * before reaching the tail. A near miss must not attach automatically. */
    for(int node=0;node<HT_ROPE_NODES-1;++node) {
        ht_spawn(true);ht.x=ht.traversal.rope_px[node];ht.y=ht.traversal.rope_py[node]+24*256;
        ht.grounded=false;int feet=ht.y;
        assert(ht_traversal_interact() && ht.traversal.mode==HT_ROPE);
        int grip=ht.traversal.rope_grip;
        assert(ht_abs(ht.y-feet)<=256 && grip<ht_mech(&ht)->rope_length*256);
        ht_step_controls(1,0,false,true);
        assert(ht.traversal.rope_grip-grip<256);
        for(int step=0;step<11;++step) ht_step_controls(1,0,false,true);
        assert(ht.traversal.rope_grip>grip && ht.traversal.rope_grip<ht_mech(&ht)->rope_length*256);
        ht_step_controls(1,0,true,true);
        assert(ht.traversal.mode==HT_FREE && ht.vy<0);
    }
    ht_spawn(true);ht.x=(1454+20)*256;ht.y=170*256;ht.grounded=false;
    assert(ht_rope_catch(&ht)<0);ht_step_controls(0,0,false,true);assert(ht.traversal.mode==HT_FREE);
    /* Scene-wide framing changes projection only, preserves the normal
     * breathing coefficients, and freezes when the character stops. */
    ht.level=7;ht_spawn(true);ht.x=1700*256;
    for(int step=0;step<64;++step) ht_camera_step(true);
    assert(ht.vista==256);unsigned vista=ht.vista;ht_camera_step(false);assert(ht.vista==vista);
    ht_game plain=ht;plain.vista=0;int turn,scale,pt,ps;
    ht_camera_coefficients(&ht,&turn,&scale);ht_camera_coefficients(&plain,&pt,&ps);
    assert(turn==pt && scale==ps);
    ht_world_scale=192;assert(ht_project_x(-ht_view_margin())==0 && ht_project_x(HT_W+ht_view_margin())==HT_W);
    ht_world_scale=256;ht.level=0;ht_spawn(true);
}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY), *frame=malloc(960u*540u/4u);
    assert(memory && frame); ht_bind(memory); ht_spawn(true);
    mechanism_invariants();mechanism_recovery();camera_invariants();visible_background_depth();landscape_contact_and_pull();eroded_cliffs_and_grotto();terrain_and_rope_invariants();
    assert(((uintptr_t)ht_low_scene&15u)==0);
    /* Prove clamp-free affine taps stay inside the source over a complete
     * rotation/zoom cycle. Linear coordinates attain extrema at row ends. */
    for(unsigned phase=0;phase<2048;++phase) {
        int turn=ht_sway_wave(phase)*43/32;
        int scale=ht_min(4096-(ht_sway_wave(phase/2+768)+256)*4/3,4096-2*ht_abs(turn));
        for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y) for(int edge=0;edge<2;++edge) {
            int x=edge?HT_W-ht_visible_left[y]-1:ht_visible_left[y];
            int u=(HT_W/2)*4096+(x-HT_W/2)*scale+(y-HT_H/2)*turn;
            int v=(HT_H/2)*4096-(x-HT_W/2)*turn+(y-HT_H/2)*scale;
            assert(u>=0 && (u>>12)+1<HT_W && v>=0 && (v>>12)+1<HT_H);
        }
    }
    /* Walk the entire route using the same fixed-step physics and hold jump
     * from each takeoff. A route that silently respawns cannot pass. */
    unsigned visited=1;
    const unsigned required_mechanics[HT_LEVELS]={40,4,5,12,16,5,6,28,14,20};
    for(int tick=0;tick<HT_LEVELS*4500 && !ht.laps;++tick) {
        walk_route_tick();
        if(ht.level!=walk_level) assert(walk_mechanics==required_mechanics[walk_level]);
        visited|=1u<<ht.level;
    }
    assert(visited==(1u<<HT_LEVELS)-1 && ht.laps==1 && ht.deaths==0);
    assert(ht.evidence==((1u<<30)-1));
    assert(ht.level==0 && ht.story_x==0);
    assert(ht.x==95*256 && ht.checkpoint==0);
    for(unsigned level=0;level<HT_LEVELS;++level) {
        ht.level=level;
        /* A failed jump returns to the latest checkpoint, not the start. */
        ht.story_x=2700; ht.checkpoint=6; ht.x=2180*256; ht.y=501*256; ht.vy=2400;
        ht_step(0,false,false);
        assert(ht.level==level && ht.checkpoint==6 && ht.story_x==2700);
        assert(ht.x==(ht_land[6].left+35)*256 && ht.grounded);
        /* Time aloft cannot kill; only passing the world bottom can. */
        ht_spawn(true);ht.x=410*256;ht.y=-500*256;ht.vy=0;ht.grounded=false;
        unsigned deaths=ht.deaths;
        for(int t=0;t<40;++t) ht_step(0,false,false);
        assert(ht.deaths==deaths);
        ht.y=501*256;ht_step(0,false,false);assert(ht.deaths==deaths+1);
    }
    ht.level=0; ht_spawn(true);
    /* Corner contact catches both approaches, never auto-climbs, and a drop
     * cannot immediately catch the same edge. Climbing ends on its solid. Use the exposed island banks,
     * not parcel seams inside a continuous hillside. */
    for(int side=-1;side<=1;side+=2) {
        ht_spawn(true); const ht_platform *p=&ht_land[5];
        int edge=side>0?p->left:p->right;
        ht.x=(edge-side*8)*256;ht.y=(ht_surface_at(&ht,5,side>0?p->left:p->right-1)+20)*256;
        ht.grounded=false;ht.vx=side*640;ht.vy=100;
        ht_step_controls(side,0,false,false);
        assert(ht.traversal.mode==HT_LEDGE && !ht.grounded);
        int hang_x=ht.x,hang_y=ht.y;
        for(int k=0;k<25;++k) ht_step_controls(side,0,false,false);
        assert(ht.x==hang_x && ht.y==hang_y && ht.traversal.mode==HT_LEDGE);
        assert(ht_traversal_interact());
        for(int k=0;k<32;++k) ht_step_controls(0,0,false,false);
        assert(ht.traversal.mode==HT_FREE && ht.grounded && ht.y==ht_surface_at(&ht,5,ht.x/256)*256);
        ht.x=(edge-side*8)*256;ht.y=(ht_surface_at(&ht,5,side>0?p->left:p->right-1)+20)*256;
        ht.grounded=false;ht.vx=side*640;ht.vy=100;
        ht_step_controls(side,0,false,false);assert(ht.traversal.mode==HT_LEDGE);
        ht_step_controls(0,1,false,false);
        assert(ht.traversal.mode==HT_FREE && ht.traversal.drop_cooldown);
    }
    /* Radius-aligned feet, gradual mass acceleration and rolling inertia. */
    ht_spawn(true);ht.x=ht.traversal.ball_x;ht.y=(220-2*HT_BALL_RADIUS)*256;
    ht.grounded=false;ht_step(0,false,false);
    assert(ht.traversal.support==1 && ht.y==ht.traversal.ball_y-HT_BALL_RADIUS*256);
    ht_spawn(true);ht.x=(210-HT_BALL_RADIUS-5)*256;ht.y=ht_surface_at(&ht,0,ht.x/256)*256;
    assert(ht_traversal_interact() && ht.traversal.mode==HT_ROLL);
    ht_step(1,false,false);assert(ht.traversal.ball_vx==14);
    for(int i=0;i<20;++i) ht_step(1,false,false);
    int ball_x=ht.traversal.ball_x,rotation=ht.traversal.ball_roll;
    assert(ht_traversal_interact());ht_step(0,false,false);
    assert(ht.traversal.ball_x>ball_x && ht.traversal.ball_roll>rotation);
    ht.level=1;ht_spawn(true);ht.x=(1220-HT_CRATE_HALF-5)*256;ht.y=100*256;
    assert(ht_traversal_interact() && ht.traversal.mode==HT_CRATE);
    for(int i=0;i<24;++i) ht_step(1,false,false);
    assert(ht.traversal.crate_vx==192);
    assert(ht_traversal_interact());
    for(int i=0;i<8;++i) ht_step(0,false,false);
    assert(ht.traversal.crate_vx==0);
    /* Loose weights cannot lower whole landscapes. These former span sites
     * are fixed terrain, with physical stones/crates still simulated. */
    const unsigned fixed_levels[]={2,5,9};const int fixed_sites[]={5,1,2};
    for(int n=0;n<3;++n) {
        ht.level=fixed_levels[n];ht_spawn(true);
        assert(!ht_mech(&ht)->plate_kind && ht_mech(&ht)->bridge==-1);
        int site=fixed_sites[n],x=(ht_land[site].left+ht_land[site].right)/2;
        int top=ht_surface_at(&ht,site,x);
        ht.x=x*256;ht.y=top*256;ht.grounded=true;
        for(int tick=0;tick<40;++tick)ht_step(0,false,false);
        assert(ht_surface_at(&ht,site,x)==top && !ht.traversal.bridge_open);
    }
    /* All boats stop with their entire hull inside the channel. The rower
     * sits at its centre; disembarking and coasting never move a bank. */
    for(unsigned level=0;level<HT_LEVELS;++level) if(ht_mechanics_by_level[level].boat_right) {
        ht.level=level;ht_spawn(true);const ht_mechanics *m=ht_mech(&ht);
        ht.x=(m->water_left-3)*256;ht.y=m->boat_deck*256;ht.grounded=true;
        assert(ht_traversal_interact() && ht.traversal.mode==HT_BOAT);
        assert(ht.x==ht.traversal.boat_x);
        for(int i=0;i<420;++i) {
            ht_step(1,false,false);
            assert(ht.x==ht.traversal.boat_x);
            assert(ht.traversal.boat_x+ht_boat_half(&ht)*256<=m->water_right*256);
        }
        assert(ht.traversal.boat_vx==0);
        for(int i=0;i<420;++i) ht_step(-1,false,false);
        assert(ht.traversal.boat_x-ht_boat_half(&ht)*256>=m->water_left*256 && !ht.traversal.boat_vx);
    }
    /* Ladders require vertical intent, not A. Horizontal/neutral jumps pass
     * freely, midair contact catches in either direction, bottom Down is inert. */
    ht.level=1;ht_spawn(true);ht.x=400*256;
    ht_step_controls(1,0,false,false);assert(ht.traversal.mode==HT_FREE);
    ht.x=400*256;ht_step_controls(0,1,false,false);
    assert(ht.traversal.mode==HT_FREE && ht.grounded && ht.y==220*256);
    ht_step_controls(0,-1,false,false);
    assert(ht.traversal.mode==HT_LADDER && ht.y==220*256-ht_climb_speed(&ht));
    ht_step_controls(1,-1,true,true);
    assert(ht.traversal.mode==HT_FREE && ht.vy<0);
    ht_step_controls(1,-1,false,true);assert(ht.traversal.mode==HT_FREE);
    for(int vertical=-1;vertical<=1;++vertical) {
        ht_spawn(true);ht.x=387*256;ht.y=160*256;ht.grounded=false;ht.vx=640;ht.vy=-500;
        ht_step_controls(1,vertical,false,true);
        assert(ht.traversal.mode==(vertical?HT_LADDER:HT_FREE));
        if(vertical) {
            int before=ht.y;ht_step_controls(0,vertical,false,true);
            assert(ht.y==before+vertical*ht_climb_speed(&ht));
        }
    }
    ht_spawn(true);ht.x=400*256;ht.y=238*256;ht.grounded=false;
    ht_step_controls(0,-1,false,false);
    /* An invalid hand-only catch buried under the lower bank is recovered
     * to its soil; descending cannot put it back inside the ground. */
    assert(ht.traversal.mode==HT_LADDER && ht.y==220*256 && ht.grounded);
    ht_step_controls(0,1,false,false);assert(ht.traversal.mode==HT_FREE && ht.grounded && ht.y==220*256);
    ht_spawn(true);ht.x=400*256;ht.y=80*256;ht.grounded=true;
    ht_step_controls(1,-1,false,false);assert(ht.traversal.mode==HT_FREE && ht.x>400*256);
    ht.x=400*256;ht_step_controls(0,1,false,false);
    assert(ht.traversal.mode==HT_LADDER && ht.y==80*256+ht_climb_speed(&ht));
    /* Catch at the actual rope end, stay above the deck, then swing away.
     * Length constraints keep each rendered segment within two pixels. */
    ht.level=0;ht_spawn(true);ht.x=1454*256;ht.y=220*256;
    assert(ht_traversal_interact() && ht.traversal.mode==HT_ROPE);
    for(int i=0;i<25;++i) {
        ht_step_controls(1,0,false,true);
        assert(ht.x>1460*256 || ht.y<=220*256);
        for(int j=1;j<HT_ROPE_NODES;++j) {
            int dx=(ht.traversal.rope_px[j]-ht.traversal.rope_px[j-1])/256;
            int dy=(ht.traversal.rope_py[j]-ht.traversal.rope_py[j-1])/256;
            assert(dx*dx+dy*dy<=16*16);
        }
    }
    assert(ht.x>1454*256 && ht.x<1485*256); /* First push cannot whip to the far side. */
    int best=ht.x;
    for(int i=0;i<155;++i) {ht_step_controls(1,0,false,true);best=ht_max(best,ht.x);}
    assert(best<1491*256); /* Holding one direction cannot build a full crossing. */
    ht_spawn(true);ht.x=1454*256;ht.y=220*256;assert(ht_traversal_interact());
    best=ht.x;
    for(int i=0;i<180;++i) {
        ht_step_controls(ht.traversal.rope_v>=0?1:-1,0,false,true);
        best=ht_max(best,ht.x);
        if(ht.x>1500*256 && ht.traversal.rope_v>0) break;
    }
    assert(best>1500*256); /* Timed return strokes build enough amplitude. */
    ht_step_controls(1,0,true,true);assert(ht.traversal.mode==HT_FREE && ht.vx>0 && ht.vy<0);
    ht.level=0;
    ht_spawn(true);unsigned phase=ht.sway_phase;
    for(int k=0;k<30;++k) ht_step(0,false,false);
    assert(ht.sway_phase==phase);
    ht_step(1,false,false);assert(ht.sway_phase>phase);
    for(int k=0;k<20;++k) ht_step(0,false,false);
    phase=ht.sway_phase;
    for(int k=0;k<30;++k) ht_step(0,false,false);
    assert(ht.sway_phase==phase);
    ht_spawn(true);
    /* Gusts push idle feet in both directions, remain bounded, and cannot
     * move a player hanging from a ledge. Weather drawing is deterministic. */
    ht.level=7;ht_spawn(true);ht.x=2100*256;ht.y=ht_surface_at(&ht,6,2100)*256;ht.scene_evidence=2;ht.weather_amount=256;ht.ticks=120;int still=ht.x;
    ht_step(0,false,false);assert(ht.x>still && ht.x-still<=40 && ht.sway_phase==0);
    ht_spawn(true);ht.x=2100*256;ht.y=ht_surface_at(&ht,6,2100)*256;ht.scene_evidence=2;ht.weather_amount=256;ht.ticks=380;still=ht.x;
    ht_step(0,false,false);assert(ht.x<still && still-ht.x<=40 && ht.sway_phase==0);
    for(unsigned tick=0;tick<1024;++tick) {ht.ticks=tick;assert(ht_abs(ht_wind(&ht))<=40);}
    ht_game snapshot=ht;memset(ht_scene,0,HT_PIXELS);ht_weather(&snapshot);
    memcpy(frame,ht_scene,HT_PIXELS);memset(ht_scene,0,HT_PIXELS);ht_weather(&snapshot);
    assert(!memcmp(frame,ht_scene,HT_PIXELS) && !memcmp(&snapshot,&ht,sizeof(ht)));
    /* Authored weather is absent in shelter and still chapters, including
     * the final decision. Weather direction/intensity also follows exposure. */
    for(unsigned level=0;level<HT_LEVELS;++level) {
        ht.level=level;ht_spawn(true);ht.ticks=120;
        const ht_climate *c=&ht_climates[level];
        ht.x=(c->begin-1)*256;assert(!ht_exposure(&ht) && !ht_wind(&ht));
        memset(ht_scene,37,HT_PIXELS);memset(frame,37,HT_PIXELS);ht_weather(&ht);
        for(int i=0;i<HT_PIXELS;++i) assert(ht_scene[i]==37);
        if(c->end) {
            ht.x=((c->begin+c->end)/2)*256;assert(!ht_weather_target(&ht) && !ht_exposure(&ht));
            ht.x=ht_evidence_x(level,c->after)*256;
            ht.y=ht_surface_at(&ht,ht_evidence_platform(ht.level,c->after),ht.x/256)*256;
            ht.grounded=true;assert(ht_inspect()>=0 && ht_weather_target(&ht)==256);
            for(int tick=0;tick<64;++tick) ht_weather_step();
            assert(ht.weather_amount==256);
            ht.x=(c->begin+80)*256;assert(ht_exposure(&ht)==128);
            ht.x=((c->begin+c->end)/2)*256;assert(ht_exposure(&ht)==256);
            assert(ht_wind(&ht)>0);
            ht_weather(&ht);assert(checksum(ht_scene,HT_PIXELS)!=checksum(frame,HT_PIXELS));
            assert(ht_sky_ink(&ht)>0);
            if(level==7) {
                ht.weather_age=160;assert(ht_lightning(&ht)==255 && !ht_sky_ink(&ht));
                ht.weather_age=168;assert(!ht_lightning(&ht));
                ht.weather_age=380;assert(ht_lightning(&ht)==255);
                ht.weather_age=388;assert(!ht_lightning(&ht));
            }
            ht.x=(c->end+1)*256;assert(!ht_exposure(&ht) && !ht_wind(&ht));
            ht.x=ht_evidence_x(level,c->until)*256;
            ht.y=ht_surface_at(&ht,ht_evidence_platform(ht.level,c->until),ht.x/256)*256;
            assert(ht_inspect()>=0 && !ht_weather_target(&ht) && !ht_lightning(&ht));
            for(int tick=0;tick<32;++tick) ht_weather_step();
            assert(!ht.weather_amount);
            ht_spawn(false);assert(!ht_weather_target(&ht));
            ht.x=ht_evidence_x(level,c->after)*256;
            ht.y=ht_surface_at(&ht,ht_evidence_platform(ht.level,c->after),ht.x/256)*256;
            assert(ht_inspect()>=0 && !ht_weather_target(&ht));
            uint32_t archive=ht.evidence;ht_spawn(true);
            assert(ht.evidence==archive && !ht.scene_evidence);
            ht.x=ht_evidence_x(level,c->after)*256;
            ht.y=ht_surface_at(&ht,ht_evidence_platform(ht.level,c->after),ht.x/256)*256;
            assert(ht_inspect()>=0 && ht_weather_target(&ht)==256);
        }
        for(int scene=0;scene<2;++scene) {
            ht.x=ht_landmark_x(level,scene)*256;ht.y=ht_surface_at(&ht,scene?6:3,ht.x/256)*256;
            ht.grounded=true;assert(ht_observe() && ht.observation==level*2+scene+1);
            for(int line=0;line<2;++line) assert(strlen(ht_observations[level*2+scene][line])*6<=332);
        }
    }
    /* A roof shelters the space below it; dry chapters add no particles. */
    ht.level=1;ht_spawn(true);ht.camera=ht.camera_y=0;
    assert(ht_weather_floor(&ht,640)==-60);
    memset(ht_scene,37,HT_PIXELS);ht.x=1700*256;ht.scene_evidence=2;ht.weather_amount=256;ht_weather(&ht);
    for(int y=80;y<HT_H;++y) assert(ht_scene[y*HT_W+400]==37);
    /* Prove the stronger transform is visible and repeatable independently
     * of world physics, and that its zoom has approximately a twenty-percent range. */
    ht.level=0;ht_spawn(true);
    for(int i=0;i<HT_PIXELS;++i) ht_temp[i]=(uint8_t)ht_hash((unsigned)i);
    memcpy(frame,ht_temp,HT_PIXELS);ht_sway_into(ht_temp,256);
    assert(memcmp(frame+HT_W*40,ht_scene+HT_W*40,HT_W*100));
    int saved_x=ht.x,saved_y=ht.y;ht_sway_into(ht_temp,1024);
    assert(ht.x==saved_x && ht.y==saved_y);
    assert((ht_sway_wave(1024/2+768)+256)*4/3==682);
    ht.level=0;ht_spawn(true);
    /* Explicit authored solutions, not answers read out of game definitions. */
    for(unsigned level=0;level<HT_LEVELS;++level) {
        ht.level=level; ht_spawn(true);
        assert(!ht_interact()); /* Too far away. */
        ht.x=HT_PUZZLE_FIRST*256; ht.grounded=false; assert(!ht_interact());
        ht.x=HT_GOAL*256; ht.y=ht_land[9].top*256; ht.vy=0;
        ht_step(1,false,false);
        assert(ht.level==level && ht.x==HT_PUZZLE_GATE*256 && !ht.puzzle.solved);
        for(unsigned k=0;k<strlen(walk_solutions[level]);++k) {
            ht.x=(HT_PUZZLE_FIRST+(walk_solutions[level][k]-'0')*HT_PUZZLE_SPACING)*256;
            ht.y=ht_land[9].top*256; ht.grounded=true; assert(ht_interact());
        }
        assert(ht.puzzle.solved);
        unsigned builds=ht_cache_builds;
        assert(!ht_interact() && ht_cache_builds==builds); /* Solved state latches. */
        ht.checkpoint=9; ht_puzzle_state saved=ht.puzzle; ht_spawn(false);
        assert(!memcmp(&saved,&ht.puzzle,sizeof(saved)) && ht.checkpoint==9);
        if(level==HT_LEVELS-1) {
            ht.x=HT_PUZZLE_GATE*256; ht.y=ht_land[9].top*256; ht.grounded=true;
            assert(ht_decide(2)); ht.verdict_read=true;
        }
        for(int tick=0;tick<48;++tick) ht_step(0,false,false);
        ht.x=HT_GOAL*256; ht.y=ht_land[9].top*256; ht.vy=0; ht_step(1,false,false);
        assert(ht.level==(level+1)%HT_LEVELS && !ht.puzzle.solved && ht.puzzle.progress==0);
    }
    ht.level=8; ht_spawn(true); ht.puzzle.stage=1; ht.grounded=true;ht.y=ht_land[9].top*256;
    ht.x=HT_PUZZLE_FIRST*256; assert(ht_interact());
    assert(ht.puzzle.wrong && ht.puzzle.progress==0);
    ht.x=(HT_PUZZLE_FIRST+HT_PUZZLE_SPACING)*256; assert(ht_interact());
    assert(!ht.puzzle.wrong && ht.puzzle.progress==1);
    ht.checkpoint=9; ht_spawn(false); assert(ht.puzzle.progress==1);
    ht.level=0; ht_spawn(true);
    for(unsigned level=0;level<HT_LEVELS;++level) {
        assert(strlen(ht_puzzles[level].name)*6<=330);
        for(int line=0;line<2;++line) {
            assert(strlen(ht_puzzles[level].reveal[line])*6<=366);
        }
    }
    /* Discovery requires proximity and grounded feet; records survive death
     * and chapter changes without revealing missing entries. */
    ht.evidence=0; memset(ht_scene,255,HT_PIXELS); ht_draw_journal(&ht,1);
    memcpy(frame,ht_scene,HT_PIXELS);
    ht.evidence=1; memset(ht_scene,255,HT_PIXELS); ht_draw_journal(&ht,1);
    assert(!memcmp(frame,ht_scene,HT_PIXELS)); /* Another find cannot reveal this page. */
    ht.evidence=2; memset(ht_scene,255,HT_PIXELS); ht_draw_journal(&ht,1);
    assert(memcmp(frame,ht_scene,HT_PIXELS));
    ht.evidence=0;
    for(unsigned level=0;level<HT_LEVELS;++level) {
        ht.level=level; ht_spawn(true); assert(ht_inspect()==-1);
        for(int item=0;item<3;++item) {
            unsigned page=level*3u+(unsigned)item;
            assert(!ht_evidence_found(&ht,page));
            ht.x=ht_evidence_x(level,item)*256;
            ht.y=ht_surface_at(&ht,ht_evidence_platform(ht.level,item),ht.x/256)*256;
            ht.grounded=false; assert(ht_inspect()==-1);
            ht.grounded=true; assert(ht_inspect()==(int)page);
            assert(ht_evidence_found(&ht,page));
            uint32_t found=ht.evidence;
            assert(ht_inspect()==(int)page && ht.evidence==found);
            const ht_evidence_record *r=&ht_evidence[level][item];
            assert(strlen(r->title)*6<348);
            for(int line=0;line<4;++line) assert(strlen(r->lines[line])*6<=348);
            ht_spawn(false); assert(ht.evidence==found);
        }
    }
    assert(ht.evidence==((1u<<30)-1));
    ht_spawn(true); assert(ht.evidence==((1u<<30)-1));
    ht.level=0; ht_spawn(true);
    /* Narration milestones, glyph bounds and dirty-row coverage. */
    assert(ht_story_beat(0)==0 && ht_story_beat(1099)==0);
    assert(ht_story_beat(1100)==1 && ht_story_beat(2599)==1 && ht_story_beat(2600)==2);
    for(unsigned level=0;level<HT_LEVELS;++level) for(unsigned beat=0;beat<3;++beat) {
        ht.level=level; ht.story_x=beat==2?2600:beat==1?1100:0;
        assert(strlen(ht_chapters[level].title)*12<=249);
        for(unsigned line=0;line<2;++line) {
            const char *t=ht_chapters[level].lines[beat][line];
            assert(*t && strlen(t)*6<=366);
            for(;*t;++t) assert((*t>='A' && *t<='Z') || *t==' ' || *t=='.' || *t=='?' || *t=='-');
        }
        memset(ht_scene,255,HT_PIXELS); ht_narration(&ht);
        unsigned letters=0;
        for(int y=0;y<HT_H;++y) for(int x=0;x<HT_W;++x) if(ht_scene[y*HT_W+x]==85) {
            ++letters; assert(y>=233 && y<=249 && x>=57 && x<423);
        }
        assert(letters>0); ht_framed=true; ht_pack_mono(frame,120);
        for(unsigned y=0;y<540;++y) if(y<ht_dirty_top || y>=ht_dirty_top+ht_dirty_height)
            for(int x=0;x<120;++x) assert(frame[y*120+x]==255);
    }
    ht.level=0; ht_spawn(true);
    /* Render representative camera positions twice: deterministic, bounds
     * checked under ASan/UBSan, all four native tones represented. */
    const int positions[]={95,330,950,1660,2310,3130};
    /* Exhaustive normalization/quantization checks retain original arithmetic. */
    for(unsigned n=1;n<=19;++n) for(unsigned sum=0;sum<=255*n;++sum)
        assert(ht_average((int)sum,(0x1000000u+n-1u)/n)==sum/n);
    for(int value=0;value<256;++value) for(int rank=0;rank<64;++rank) {
        int q=value/85,rem=value-q*85;
        if(q<3 && rem*64>rank*85+42) ++q;
        assert(ht_quantize(value,rank)==q);
    }
    /* Cached shifts must equal a full fresh convolution, including reversals,
     * culling boundaries, both blur footprints, and large camera teleports. */
    uint8_t *expected=malloc(HT_PIXELS); assert(expected);
    for(int i=0;i<100;++i) {
        int camera=i<40?i*7:i<80?(79-i)*7:(i*137)%2860;
        ht.camera=camera*256; ht.x=(camera+165)*256;
        ht.y=(170+i%40)*256;
        ht_render_scene(); memcpy(expected,ht_scene,HT_PIXELS);
        memset(ht_cache_valid,0,sizeof(ht_cache_valid)); ht_render_scene();
        assert(!memcmp(expected,ht_scene,HT_PIXELS));
    }
    /* Cropped foreground convolution must retain the full filter's pixels. */
    for(int i=0;i<HT_PIXELS;++i) ht_raw[i]=(uint8_t)ht_hash((uint32_t)i);
    ht_blur_region(ht_raw,expected,4,0,HT_W);
    ht_blur_rect(ht_raw,ht_wide,4,HT_BORDER,HT_W-HT_BORDER,HT_BORDER,HT_H-HT_BORDER);
    for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y)
        assert(!memcmp(expected+y*HT_W+HT_BORDER,ht_wide+y*HT_W+HT_BORDER,HT_W-2*HT_BORDER));
    /* Quarter-resolution reconstruction must preserve source anchors exactly,
     * keep flat fills bit-identical, and keep integer inference deterministic. */
    memset(ht_recon_scene,73,HT_RECON_PIXELS); ht_reconstruct_low_scene();
    for(int i=0;i<HT_SCENE_PIXELS;++i) assert(ht_low_scene[i]==73);
    for(int i=0;i<HT_RECON_PIXELS;++i) ht_recon_scene[i]=(uint8_t)ht_hash((uint32_t)i);
    ht_reconstruct_low_scene(); memcpy(expected,ht_low_scene,HT_SCENE_PIXELS);
    ht_reconstruct_low_scene(); assert(!memcmp(expected,ht_low_scene,HT_SCENE_PIXELS));
    for(int y=0;y<HT_RECON_H;++y) for(int x=0;x<HT_RECON_W;++x)
        assert(ht_low_scene[(2*y)*HT_SCENE_W+2*x]==ht_recon_scene[y*HT_RECON_W+x]);
    {
        const uint8_t patch[HT_NN_INPUTS]={0,0,0,0,255,255,0,255,255};
        int16_t residual[HT_NN_OUTPUTS];
        ht_nn_residual(patch,residual);
        assert(residual[0]==-48 && residual[1]==-13 && residual[2]==108);
    }
    /* Upscaling retains exact samples, clamps last rows/columns, and cannot
     * overflow on white/black transitions. Reference is per output pixel. */
    for(int i=0;i<HT_SCENE_PIXELS;++i) ht_low_scene[i]=(uint8_t)ht_hash((uint32_t)i);
    ht_upscale_scene();
    for(int y=0;y<HT_H;++y) for(int x=0;x<HT_W;++x) {
        int sx=x/2,sy=y/2,nx=ht_min(sx+1,HT_SCENE_W-1),ny=ht_min(sy+1,HT_SCENE_H-1);
        int a=ht_low_scene[sy*HT_SCENE_W+sx],b=ht_low_scene[ny*HT_SCENE_W+sx];
        if(x%2) { a=(a+ht_low_scene[sy*HT_SCENE_W+nx])/2; b=(b+ht_low_scene[ny*HT_SCENE_W+nx])/2; }
        assert(ht_scene[y*HT_W+x]==(y%2?(a+b)/2:a));
    }
    /* Border and fade are symmetric and leave the central scene unchanged. */
    memset(ht_scene,100,HT_PIXELS); ht_vignette();
    for(int y=0;y<HT_H;++y) for(int x=0;x<HT_W;++x) {
        int ex=ht_min(x,HT_W-1-x),ey=ht_min(y,HT_H-1-y),edge=ht_min(ex,ey);
        if(ex<HT_MASK_RADIUS && ey<HT_MASK_RADIUS) {
            int dx=HT_MASK_RADIUS-ex,dy=HT_MASK_RADIUS-ey,d=dx*dx+dy*dy,r=0;
            while((r+1)*(r+1)<=d) ++r;
            edge=HT_MASK_RADIUS-r;
        }
        int alpha=ht_clamp(edge-HT_BORDER,0,HT_FADE-HT_BORDER)*255/(HT_FADE-HT_BORDER);
        int wanted=255-(155*alpha+127)/255;
        assert(ht_scene[y*HT_W+x]==wanted);
    }
    /* Fast packing of hidden pixels equals the generic packer, in both modes. */
    uint8_t *generic=malloc(960u*540u/4u); assert(generic);
    for(int mono=0;mono<2;++mono) {
        int stride=mono?120:240;
        ht_framed=true; ht_pack_format(frame,stride,mono);
        ht_framed=false; ht_pack_format(generic,stride,mono);
        assert(!memcmp(frame,generic,(size_t)stride*540u));
    }
    ht_framed=false;ht_pack_mono(frame,120);ht_pack_mono(generic+1,120);
    assert(!memcmp(frame,generic+1,64800)); // Unaligned output retains byte-safe fallback.

    free(generic);
    free(expected);
    clock_t start=clock();
    for(unsigned i=0;i<sizeof(positions)/sizeof(positions[0]);++i) {
        ht_spawn(true); ht.x=positions[i]*256;
        ht.camera=ht_clamp(positions[i]-165,0,HT_GOAL-340)*256;
        ht_render_scene();
        ht_pack_mono(frame,120);
        assert(ht_dirty_top>0 && ht_dirty_height>0 && ht_dirty_top+ht_dirty_height<540);
        for(unsigned y=0;y<540;++y) if(y<ht_dirty_top || y>=ht_dirty_top+ht_dirty_height)
            for(int x=0;x<120;++x) assert(frame[y*120+x]==255);
        ht_pack(frame,240);
        uint32_t first=checksum(frame,960u*540u/4u);
        ht_render_scene(); ht_pack(frame,240);
        assert(first==checksum(frame,960u*540u/4u));
        unsigned tones=0;
        for(size_t n=0;n<960u*540u/4u;++n) for(int shift=0;shift<8;shift+=2)
            tones|=1u<<((frame[n]>>shift)&3);
        assert(tones==15);
    }
    printf("Hollow Trail: complete route, loop, checkpoints, trench hazards, deterministic 2bpp frames PASS (%.1f ms/host frame)\n",
           (double)(clock()-start)*1000.0/CLOCKS_PER_SEC/12.0);
    free(frame); free(memory); return 0;
}
