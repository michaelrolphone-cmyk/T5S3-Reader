#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
#include "../../Apps/hollow_trail_cutscene.inc"

static uint32_t image_hash(void) {
    uint32_t h=2166136261u;
    for(size_t i=0;i<HT_PIXELS;++i){h^=ht_scene[i];h*=16777619u;}
    return h;
}
static void render_face(int face,uint32_t *hash) {
    ht_cutscene_camera camera={HT_CUT_VIEW_FORWARD_X,0,0,48,256,420,138,6};
    ht_cutscene_fill(6);ht_cutscene_objects_reset();
    ht_cutscene_queue_actor(220,0,0,HT_CUT_POSE_IDLE,face,7);
    ht_cutscene_render_objects(&camera);*hash=image_hash();
}
static void assert_prior_raster_independent(const ht_cutscene_state *s) {
    size_t bytes=ht_camera_mode==HT_CAMERA_NATIVE?HT_NATIVE_PIXELS:HT_PIXELS;
    uint8_t *out=ht_camera_mode==HT_CAMERA_NATIVE?ht_native_a:ht_scene_low;
    uint8_t *expected=malloc(bytes);assert(expected);
    memset(out,0,bytes);ht_cutscene_render(s);memcpy(expected,out,bytes);
    memset(out,255,bytes);ht_cutscene_render(s);
    assert(!memcmp(expected,out,bytes));free(expected);
}
static void rain_tank_weather(void) {
    /* The burnt fuse enables a second rain pass in live gameplay. The refuge
     * must shelter that pass too, including slanted drops at either edge. */
    static const int cameras[]={1400,1640,1890},scales[]={176,256,384};
    memset(&ht,0,sizeof(ht));ht.level=1;ht_select_level(1);ht_spawn(true);
    ht.x=1840*256;ht.y=70*256;ht.camera_y=-110*256;
    ht.scene_evidence=2;ht.weather_amount=256;
    for(unsigned mode=0;mode<2;++mode) {
        ht_native_active=mode!=0;ht_native_foreground_half_y=false;
        ht_scene=mode?ht_native_a:ht_scene_low;
        int raster=mode?2:1,width=HT_W*raster;
        size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;
        for(unsigned camera=0;camera<3;++camera)for(unsigned scale=0;scale<3;++scale) {
            ht.camera=cameras[camera]*256;ht_world_scale=scales[scale];
            /* Stay one projected pixel inside the roof edge to exclude the
             * raster primitive's endpoint rounding, not falling rain. */
            int left=ht_max(0,ht_project_x(1751-cameras[camera])*raster+1);
            int right=ht_min(width,ht_project_x(1928-cameras[camera])*raster-1);
            int top=ht_max(0,ht_project_y(125)*raster);
            int bottom=ht_min(HT_H*raster,ht_project_y(176)*raster);
            unsigned outside=0,drips=0;
            for(unsigned tick=0;tick<270;++tick) {
                ht.ticks=tick;ht_game retained=ht;
                memset(ht_scene,0,bytes);ht_weather(&ht);
                assert(!memcmp(&ht,&retained,sizeof(ht)));
                for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)
                    assert(ht_scene[y*width+x]==0);
                if(tick%27==0) {
                    for(size_t i=0;i<bytes;++i)outside+=ht_scene[i]!=0;
                    memset(ht_scene,0,bytes);ht_city_rain_tank(&ht);
                    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
                        assert(ht_scene[y*width+x]!=119); /* Local falling rain. */
                        drips+=ht_scene[y*width+x]==122; /* Authored belly drips. */
                    }
                }
            }
            assert(outside>0); /* The fix must not disable the weather pass. */
            if(left<right)assert(drips>0); /* Close zoom can move all cover off-screen. */
        }
    }
    ht_world_scale=256;ht_native_active=false;ht_scene=ht_scene_low;
}
static void oil_service_lamps(void) {
    /* Both actual Oil Fields landmarks are service lights, not invitations
     * repeating the city signal. Check their complete former flash cycle. */
    memset(&ht,0,sizeof(ht));ht.level=2;ht_select_level(2);ht_spawn(true);
    for(unsigned mode=0;mode<3;++mode)for(int scene=0;scene<2;++scene) {
        ht_native_active=mode!=0;ht_native_foreground_half_y=mode==2;
        ht_scene=mode?ht_native_a:ht_scene_low;ht_world_scale=256;
        int raster=mode?2:1,width=HT_W*raster,world=ht_landmark_x(2,scene);
        size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;
        ht.camera=(world-200)*256;
        int base=ht_buried_base(&ht,scene?6:3,world,62);ht.camera_y=(base-180)*256;
        for(unsigned tick=0;tick<144;++tick) {
            ht.ticks=tick;ht_game retained=ht;memset(ht_scene,127,bytes);ht_story_landmarks(&ht);
            assert(!memcmp(&ht,&retained,sizeof(ht)));
            assert(ht_scene[(180-57)*raster*width+258*raster]<50);
            assert(ht_scene[(180-65)*raster*width+252*raster]>200); /* Housing exists. */
        }
    }
    /* Other chapter beacons keep three short flashes; the ending choice can
     * still extinguish the final signal. */
    ht_native_active=false;ht_native_foreground_half_y=false;ht_scene=ht_scene_low;
    for(unsigned level=1;level<HT_LEVELS;++level)if(level!=2) {
        ht.level=level;ht.verdict=0;unsigned flashes=0,lit_ticks=0;bool prior=false;
        for(unsigned tick=0;tick<144;++tick) {
            ht.ticks=tick;memset(ht_scene,127,HT_PIXELS);ht_signal(80,150,&ht);
            bool lit=ht_scene[93*HT_W+80]<50;lit_ticks+=lit;if(lit&&!prior)++flashes;prior=lit;
        }
        assert(flashes==3 && lit_ticks==27);
    }
    ht.level=9;ht.verdict=1;ht.ticks=0;ht_signal(80,150,&ht);assert(ht_scene[93*HT_W+80]>150);
}
static void mill_barred_door(void) {
    /* The novella's closed door and broken-shutter entry must read as two
     * different openings: a timber bar interrupts a dark doorway recess. */
    static const int scales[]={272,384},cameras[]={900,1040};
    memset(&ht,0,sizeof(ht));ht.level=0;ht_select_level(0);ht_spawn(true);
    ht.camera_y=40*256;
    for(unsigned mode=0;mode<3;++mode) {
        ht_native_active=mode!=0;ht_native_foreground_half_y=mode==2;
        ht_scene=mode?ht_native_a:ht_scene_low;
        int raster=mode?2:1,width=HT_W*raster;
        size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;
        for(unsigned camera=0;camera<2;++camera)for(unsigned scale=0;scale<2;++scale) {
            ht.camera=cameras[camera]*256;ht_world_scale=scales[scale];
            ht_game retained=ht;memset(ht_scene,0,bytes);ht_forest_mill(&ht);
            assert(!memcmp(&ht,&retained,sizeof(ht)));
            int x=ht_project_x(1095+142-cameras[camera])*raster;
            int upper=ht_project_y(218-40-49)*raster;
            int bar=ht_project_y(218-40-38)*raster;
            unsigned recess=0,timber=0;
            for(int dx=-2*raster;dx<=2*raster;++dx) {
                recess+=ht_scene[upper*width+x+dx];
                timber+=ht_scene[bar*width+x+dx];
            }
            assert(recess>timber+(unsigned)(4*raster+1)*40u);
        }
    }
    ht_world_scale=256;ht_native_active=false;ht_native_foreground_half_y=false;ht_scene=ht_scene_low;
}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY);assert(memory);ht_bind(memory);ht_bind_native(memory);
    rain_tank_weather();
    mill_barred_door();
    oil_service_lamps();
    ht.level=0;memset(&ht,0,sizeof(ht));ht_spawn(true);
    ht_cutscene_begin(HT_CUTSCENE_INTRO);
    assert(ht_cutscene.active && ht_cutscene.tick==0);
    assert(ht_cutscene_cue_at(&ht_cutscene)->scene==HT_CUT_KITCHEN);
    assert(ht_cutscene_cue_at(&ht_cutscene)->view==HT_CUT_VIEW_FORWARD_X);
    assert(ht_cutscene_track(&ht_cutscene)->duration==HT_INTRO_TICKS);
    assert(HT_INTRO_TICKS==2280u);
    /* Reading beats now have deliberate dwell instead of sub-four-second cuts. */
    for(unsigned i=0;i<6;++i) assert(ht_intro_cues[i].end-ht_intro_cues[i].start>=150);

    ht_cutscene_camera camera=ht_cutscene_camera_at(&ht_cutscene);
    ht_cutscene_projection near=ht_cutscene_project_forward(&camera,(ht_cut_vec3){100,80,0});
    ht_cutscene_projection far=ht_cutscene_project_forward(&camera,(ht_cut_vec3){800,80,0});
    assert(near.scale>far.scale);
    assert(ht_abs(near.x-240)>ht_abs(far.x-240));
    assert(ht_cutscene_depth_tone(&camera,near.depth,220)>ht_cutscene_depth_tone(&camera,far.depth,220));

    ht_cutscene_state frame=ht_cutscene;ht_cutscene_render(&frame);uint32_t kitchen=image_hash();assert(kitchen);
    while(ht_cutscene.tick<215)assert(!ht_cutscene_step(&ht_cutscene));
    assert(ht_cutscene_cue_at(&ht_cutscene)->scene==HT_CUT_KITCHEN);
    assert(ht_cutscene_intro_flash(&ht_cutscene)>0);

    while(ht_cutscene.tick<420)assert(!ht_cutscene_step(&ht_cutscene));
    assert(ht_cutscene_cue_at(&ht_cutscene)->scene==HT_CUT_GRASS);
    assert(ht_cutscene_cue_at(&ht_cutscene)->view==HT_CUT_VIEW_FORWARD_X);
    frame=ht_cutscene;ht_cutscene_render(&frame);uint32_t grass=image_hash();
    assert(grass && grass!=kitchen);

    uint32_t front,back,quarter;render_face(HT_CUT_FACE_FRONT,&front);
    render_face(HT_CUT_FACE_BACK,&back);render_face(HT_CUT_FACE_RIGHT_3Q,&quarter);
    assert(front!=back && back!=quarter && front!=quarter);

    /* Profile continuity begins at the house door after packing. */
    while(ht_cutscene.tick<1540)assert(!ht_cutscene_step(&ht_cutscene));
    assert(ht_cutscene_cue_at(&ht_cutscene)->scene==HT_CUT_ORCHARD);
    assert(ht_cutscene_cue_at(&ht_cutscene)->view==HT_CUT_VIEW_PROFILE);
    assert(ht_cutscene_actor_x(&ht_cutscene)>=118 && ht_cutscene_actor_x(&ht_cutscene)<190);
    frame=ht_cutscene;ht_cutscene_render(&frame);uint32_t doorway=image_hash();
    assert(doorway && doorway!=grass);

    /* At this point the actor is still before x=560, but the forest ahead is
     * already in the raster. This guards against the reported pop-in. */
    while(ht_cutscene.tick<1880)assert(!ht_cutscene_step(&ht_cutscene));
    assert(ht_cutscene_actor_x(&ht_cutscene)<560);
    frame=ht_cutscene;ht_cutscene_render(&frame);
    unsigned forest_ink=0;
    for(int y=28;y<198;++y)for(int x=396;x<476;++x) if(ht_scene[y*HT_W+x]>150)++forest_ink;
    assert(forest_ink>80);

    int previous=ht_cutscene_actor_x(&ht_cutscene);
    for(int i=0;i<80;++i) {
        assert(!ht_cutscene_step(&ht_cutscene));
        int x=ht_cutscene_actor_x(&ht_cutscene);assert(x>=previous);previous=x;
    }

    while(ht_cutscene.tick<HT_INTRO_TICKS-1)assert(!ht_cutscene_step(&ht_cutscene));
    assert(ht_cutscene.active && !ht_cutscene.finished);
    assert(ht_cutscene_cue_at(&ht_cutscene)->framing_to==384);
    assert(ht_cutscene_step(&ht_cutscene));
    assert(!ht_cutscene.active && ht_cutscene.finished && ht_cutscene.tick==HT_INTRO_TICKS);
    ht.level=7;ht_cutscene_apply_handoff(&ht_cutscene);
    assert(ht.level==0 && ht.x==95*256 && ht.grounded && ht.traversal.mode==HT_FREE);
    assert(ht_scene_scale(&ht)==384);
    /* Actual non-intro track pauses a narrative arrival, preserves the entire
     * gameplay state/evidence and cannot replay when the player walks back. */
    ht_game before=ht;before.x=1069*256;before.grounded=true;
    ht=before;ht.x=1071*256;ht.y=ht_surface_at(&ht,2,1071)*256;
    ht.camera=871*256;ht.camera_y=(ht.y/256-180)*256;
    assert(ht_cutscene_mill_arrival(&before,&ht));
    ht.evidence=7;ht_game retained=ht;
    ht_cutscene_begin(HT_CUTSCENE_MILL);
    assert(!ht_cutscene_mill_arrival(&before,&ht));
    for(unsigned mode=0;mode<2;++mode) {
        ht_camera_mode=mode?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
        for(unsigned tick=0;tick<340;tick+=37) {
            ht_cutscene.tick=(uint16_t)tick;assert_prior_raster_independent(&ht_cutscene);
            assert(!memcmp(&ht,&retained,sizeof(ht)));
            assert(ht_native_active==(mode!=0));
        }
    }
    ht_cutscene.tick=339;assert(ht_cutscene_step(&ht_cutscene));
    ht_cutscene_apply_handoff(&ht_cutscene);
    assert(!memcmp(&ht,&retained,sizeof(ht)));
    /* City arrival is earned by the actual forest gate transition, not by
     * a timer, a debug chapter choice, or crossing some arbitrary coordinate. */
    ht_game gate=ht,city=ht;gate.level=0;gate.x=HT_GOAL*256;
    city.level=1;gate.puzzle.solved=false;gate.puzzle.opening=48;
    assert(!ht_cutscene_city_arrival(&gate,&city));
    gate.puzzle.solved=true;assert(ht_cutscene_city_arrival(&gate,&city));
    city.level=2;assert(!ht_cutscene_city_arrival(&gate,&city));
    ht.level=1;ht_select_level(1);ht_spawn(true);retained=ht;
    ht_cutscene_begin(HT_CUTSCENE_CITY);
    city.level=1;assert(!ht_cutscene_city_arrival(&gate,&city));
    for(unsigned mode=0;mode<2;++mode) {
        ht_camera_mode=mode?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
        for(unsigned tick=0;tick<430;tick+=37) {
            ht_cutscene.tick=(uint16_t)tick;assert_prior_raster_independent(&ht_cutscene);
            assert(!memcmp(&ht,&retained,sizeof(ht)));
        }
    }
    ht_cutscene.tick=429;assert(ht_cutscene_step(&ht_cutscene));
    ht_cutscene_apply_handoff(&ht_cutscene);assert(!memcmp(&ht,&retained,sizeof(ht)));
    /* Rain-tank tableau: only an actual forward grounded crossing earns it. */
    ht_cutscene_seen=0;ht.level=1;ht_select_level(1);ht_spawn(true);
    ht.x=HT_RAIN_TANK_X*256;ht.y=ht_rain_tank_floor()*256;ht.grounded=true;
    ht.scene_evidence=2;ht.weather_amount=256; /* Actual post-fuse arrival. */
    ht.camera=ht.x-200*256;ht.camera_y=ht.y-180*256;
    before=ht;before.x-=256;assert(ht_cutscene_rain_arrival(&before,&ht));
    before.x=ht.x+256;assert(!ht_cutscene_rain_arrival(&before,&ht));
    before.x=ht.x-9*256;assert(!ht_cutscene_rain_arrival(&before,&ht));
    before.x=ht.x-256;ht.grounded=false;assert(!ht_cutscene_rain_arrival(&before,&ht));
    ht.grounded=true;ht.y-=30*256;assert(!ht_cutscene_rain_arrival(&before,&ht));
    ht.y+=30*256;ht.traversal.mode=HT_LADDER;assert(!ht_cutscene_rain_arrival(&before,&ht));
    ht.traversal.mode=HT_FREE;before.level=0;assert(!ht_cutscene_rain_arrival(&before,&ht));before.level=1;
    retained=ht;ht_cutscene_begin(HT_CUTSCENE_RAIN);
    assert(!ht_cutscene_rain_arrival(&before,&ht));
    for(unsigned mode=0;mode<2;++mode) {
        ht_camera_mode=mode?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
        for(unsigned tick=0;tick<420;tick+=37) {
            ht_cutscene.tick=(uint16_t)tick;assert_prior_raster_independent(&ht_cutscene);
            assert(!memcmp(&ht,&retained,sizeof(ht)));
        }
    }
    /* Different approach zooms must not snap to an assumed close framing. */
    for(unsigned framing=0;framing<3;++framing) {
        ht_cutscene_anchor.intimacy=(uint16_t)(framing*103);
        ht_cutscene_anchor.vista=(uint16_t)(framing*79);
        for(unsigned endpoint=0;endpoint<2;++endpoint) {
            ht_cutscene.tick=endpoint?420:0;
            ht_game view=ht_cutscene_arrival_view(&ht_cutscene);
            assert(view.camera==ht_cutscene_anchor.camera && view.camera_y==ht_cutscene_anchor.camera_y);
            assert(ht_scene_scale(&view)==ht_scene_scale(&ht_cutscene_anchor));
        }
        ht_cutscene.tick=170;ht_game view=ht_cutscene_arrival_view(&ht_cutscene);
        assert(ht_scene_scale(&view)==176);
    }
    ht_cutscene_anchor=retained;
    unsigned pulses=0;bool was=false;
    for(unsigned tick=0;tick<144;++tick) {
        bool lit=ht_rain_window_lit(tick);if(lit && !was)++pulses;was=lit;
        assert(lit==ht_rain_window_lit(tick+144));
    }
    assert(pulses==3 && !ht_rain_window_lit(54) && !ht_rain_window_lit(143));
    assert(ht_rain_window_x()>ht_mechanics_by_level[1].ladders[2].x);
    assert(ht_rain_window_x()<ht_level_land(1)[7].right);
    ht_cutscene.tick=419;assert(ht_cutscene_step(&ht_cutscene));
    ht_cutscene_apply_handoff(&ht_cutscene);assert(!memcmp(&ht,&retained,sizeof(ht)));
    /* Grass wind is continuous through its wrap in every depth plane. */
    for(unsigned tick=0;tick<1024;++tick)
        assert(ht_abs(ht_cut_grass_wind(tick+1,11)-ht_cut_grass_wind(tick,11))<=1);
    /* The register is in the authored mill hollow before the rope, not on a stump. */
    assert(ht_evidence_platform(0,1)==3 && ht_evidence_x(0,1)==1180);
    /* Final intro raster equals the live scene: no scene/camera/pose cut at
     * handoff, in both outputs. Repeated rendering never mutates live state. */
    for(unsigned mode=0;mode<2;++mode) {
        ht_camera_mode=mode?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
        ht.level=0;ht_select_level(0);ht_spawn(true);ht_cutscene_begin(HT_CUTSCENE_INTRO);
        ht_cutscene.tick=HT_INTRO_TICKS;ht_game saved=ht;
        ht_cutscene_render(&ht_cutscene);assert(!memcmp(&saved,&ht,sizeof(ht)));
        unsigned raster=mode?2:1,width=HT_W*raster;
        for(unsigned y=230*raster;y<233*raster;++y)assert(ht_scene[y*width+240*raster]==255);
        for(unsigned y=250*raster;y<253*raster;++y)assert(ht_scene[y*width+240*raster]==255);
        size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;
        uint8_t *last=malloc(bytes);assert(last);memcpy(last,ht_scene,bytes);
        ht_cutscene_apply_handoff(&ht_cutscene);ht_render_scene();ht_narration(&ht);ht_vignette();
        assert(!memcmp(last,ht_scene,bytes));
        ht_render_scene_from(&ht,false);ht_narration(&ht);ht_vignette();
        unsigned actor_pixels=0;
        for(size_t i=0;i<bytes;++i)if(last[i]>220 && ht_scene[i]<180)++actor_pixels;
        assert(actor_pixels>30); /* Equality of two empty backgrounds is insufficient. */
        free(last);
        assert(ht.x==95*256 && ht.level==0 && ht.grounded && ht.vx==0);
    }
    /* Exercise complete intro in both output rasters with bounded allocation. */
    for(unsigned mode=0;mode<2;++mode) {
        ht_camera_mode=mode?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
        ht_cutscene_begin(HT_CUTSCENE_INTRO);
        for(unsigned tick=0;tick<HT_INTRO_TICKS;tick+=31) {
            ht_cutscene.tick=(uint16_t)tick;assert_prior_raster_independent(&ht_cutscene);
            assert(ht_cut_object_count<=HT_CUT_OBJECTS_MAX);
        }
    }
    free(memory);
    puts("Hollow Trail cutscene: domestic/memory/latch beats, both rasters, continuous wind, one-shot mill/city/rain tableaus, earned gate transition, preserved gameplay and handoff PASS");
}
