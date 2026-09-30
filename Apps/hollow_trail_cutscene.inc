/* Hollow Trail cutscene timeline.
 * Cutscenes use the normal game raster primitives and articulated character,
 * but a fixed 32 ms cue track owns camera, actor pose and scene progression.
 * Live gameplay input never advances an actor while a cutscene is active. */
enum { HT_CUTSCENE_NONE, HT_CUTSCENE_INTRO };
enum { HT_CUT_KITCHEN, HT_CUT_GRASS, HT_CUT_ORCHARD };
enum { HT_CUT_POSE_IDLE, HT_CUT_POSE_WASH, HT_CUT_POSE_LOOK, HT_CUT_POSE_CROUCH,
       HT_CUT_POSE_PACK, HT_CUT_POSE_WALK };
typedef struct {
    uint16_t start,end;
    uint8_t scene,pose,caption;
    int16_t actor_from,actor_to,camera_from,camera_to;
    uint16_t scale_from,scale_to;
} ht_cutscene_cue;
typedef struct {
    const ht_cutscene_cue *cues;
    uint8_t cue_count,handoff_level;
    uint16_t duration;
} ht_cutscene_timeline;
typedef struct {
    uint8_t id;
    uint16_t tick;
    bool active,finished;
} ht_cutscene_state;

static const ht_cutscene_cue ht_intro_cues[]={
    {  0, 80,HT_CUT_KITCHEN,HT_CUT_POSE_WASH,  0,238,238,  0,  0,264,264},
    { 80,155,HT_CUT_KITCHEN,HT_CUT_POSE_LOOK,  1,238,238,  0,  0,264,276},
    {155,280,HT_CUT_GRASS,  HT_CUT_POSE_CROUCH,2,208,208,  0,  0,256,256},
    {280,375,HT_CUT_GRASS,  HT_CUT_POSE_LOOK,  3,208,208,  0,  0,256,260},
    {375,460,HT_CUT_KITCHEN,HT_CUT_POSE_IDLE,  4,238,238,  0,  0,274,260},
    {460,570,HT_CUT_KITCHEN,HT_CUT_POSE_PACK,  5,238,330,  0,  0,260,244},
    {570,760,HT_CUT_ORCHARD,HT_CUT_POSE_WALK,255,130,590,  0,330,244,234},
    {760,840,HT_CUT_ORCHARD,HT_CUT_POSE_WALK,  6,590,720,330,460,234,230},
};
#define HT_INTRO_TICKS 840u
static const ht_cutscene_timeline ht_cutscene_tracks[]={
    {NULL,0,0,0},
    {ht_intro_cues,(uint8_t)(sizeof(ht_intro_cues)/sizeof(ht_intro_cues[0])),0,HT_INTRO_TICKS}
};
static ht_cutscene_state ht_cutscene;

static const ht_cutscene_timeline *ht_cutscene_track(const ht_cutscene_state *s) {
    return s->id<sizeof(ht_cutscene_tracks)/sizeof(ht_cutscene_tracks[0])?
        &ht_cutscene_tracks[s->id]:&ht_cutscene_tracks[0];
}
static const ht_cutscene_cue *ht_cutscene_cue_at(const ht_cutscene_state *s) {
    const ht_cutscene_timeline *track=ht_cutscene_track(s);
    for(unsigned i=0;i<track->cue_count;++i)
        if(s->tick>=track->cues[i].start && s->tick<track->cues[i].end) return &track->cues[i];
    return track->cue_count?&track->cues[track->cue_count-1]:NULL;
}
static int ht_cutscene_lerp(int a,int b,int u) { return a+(b-a)*u/256; }
static int ht_cutscene_progress(const ht_cutscene_state *s,const ht_cutscene_cue *cue) {
    if(!cue || cue->end<=cue->start)return 0;
    return ht_clamp(((int)s->tick-cue->start)*256/(cue->end-cue->start),0,256);
}
static int ht_cutscene_actor_x(const ht_cutscene_state *s) {
    const ht_cutscene_cue *cue=ht_cutscene_cue_at(s);int u=ht_cutscene_progress(s,cue);
    return cue?ht_cutscene_lerp(cue->actor_from,cue->actor_to,u):0;
}
static int ht_cutscene_camera_x(const ht_cutscene_state *s) {
    const ht_cutscene_cue *cue=ht_cutscene_cue_at(s);int u=ht_cutscene_progress(s,cue);
    return cue?ht_cutscene_lerp(cue->camera_from,cue->camera_to,u):0;
}
static int ht_cutscene_scale(const ht_cutscene_state *s) {
    const ht_cutscene_cue *cue=ht_cutscene_cue_at(s);int u=ht_cutscene_progress(s,cue);
    return cue?ht_cutscene_lerp(cue->scale_from,cue->scale_to,u):256;
}
static void ht_cutscene_begin(unsigned id) {
    memset(&ht_cutscene,0,sizeof(ht_cutscene));
    if(id>=sizeof(ht_cutscene_tracks)/sizeof(ht_cutscene_tracks[0]) || !ht_cutscene_tracks[id].cue_count)return;
    ht_cutscene.id=(uint8_t)id;ht_cutscene.active=true;
}
static bool ht_cutscene_step(ht_cutscene_state *s) {
    if(!s->active)return false;
    const ht_cutscene_timeline *track=ht_cutscene_track(s);
    if(++s->tick<track->duration)return false;
    s->tick=track->duration;s->active=false;s->finished=true;return true;
}
static void ht_cutscene_apply_handoff(const ht_cutscene_state *s) {
    const ht_cutscene_timeline *track=ht_cutscene_track(s);
    ht.level=track->handoff_level;ht_spawn(true);
}

/* Triangular deterministic exposure pulse. */
static int ht_cutscene_pulse(int local,int at,int width) {
    int d=ht_abs(local-at);return d>=width?0:(width-d)*256/width;
}
static int ht_cutscene_intro_flash(const ht_cutscene_state *s) {
    const ht_cutscene_cue *cue=ht_cutscene_cue_at(s);if(!cue)return 0;
    int local=(int)s->tick-cue->start,value=0;
    if(cue->scene==HT_CUT_KITCHEN && cue->caption==1) {
        value=ht_max(value,ht_cutscene_pulse(local,10,5));
        value=ht_max(value,ht_cutscene_pulse(local,33,5));
        value=ht_max(value,ht_cutscene_pulse(local,56,5));
    } else if(cue->scene==HT_CUT_GRASS && cue->caption==2) {
        value=ht_max(value,ht_cutscene_pulse(local,33,4));
        value=ht_max(value,ht_cutscene_pulse(local,64,4));
        value=ht_max(value,ht_cutscene_pulse(local,95,4));
    } else if(cue->scene==HT_CUT_GRASS && cue->caption==3) {
        value=ht_max(value,ht_cutscene_pulse(local,18,4));
        value=ht_max(value,ht_cutscene_pulse(local,42,4));
        if(local>=64 && local<82)value=256;
    }
    return value;
}
static ht_person_pose ht_cutscene_actor_pose(int x,int feet,int pose,int phase,int facing) {
    ht_person_pose p;int f=facing?facing:1;
    p.facing=f;p.hip=ht_joint_at(x,feet-14);p.shoulder=ht_joint_at(x+f,feet-22);
    for(int i=0;i<2;++i) {
        p.foot[i]=ht_joint_at(x+(i?2:-2),feet);
        p.hand[i]=ht_joint_at(x+f*(i?2:-1),feet-12);
        p.knee[i]=-f;p.elbow[i]=f;
    }
    if(pose==HT_CUT_POSE_WASH) {
        p.hip.x=x-2*f;p.shoulder.x=x+3*f;
        p.hand[0]=ht_joint_at(x+15*f,feet-13);p.hand[1]=ht_joint_at(x+13*f,feet-10);
        p.foot[0].x=x-5*f;p.foot[1].x=x+4*f;
    } else if(pose==HT_CUT_POSE_LOOK) {
        p.shoulder.x=x+2*f;p.hand[0]=ht_joint_at(x+4*f,feet-12);p.hand[1]=ht_joint_at(x-3*f,feet-11);
    } else if(pose==HT_CUT_POSE_CROUCH) {
        p.hip=ht_joint_at(x,feet-8);p.shoulder=ht_joint_at(x+f,feet-15);
        p.foot[0]=ht_joint_at(x-7*f,feet);p.foot[1]=ht_joint_at(x+6*f,feet);
        p.hand[0]=ht_joint_at(x+8*f,feet-5);p.hand[1]=ht_joint_at(x-5*f,feet-4);
    } else if(pose==HT_CUT_POSE_PACK) {
        int q=phase%32,step=q<16?8-q:q-24,lift=q<16?0:(q-16)*(32-q)/32;
        p.shoulder.x=x+2*f;p.hand[0]=ht_joint_at(x+4*f,feet-11);p.hand[1]=ht_joint_at(x-5*f,feet-7);
        p.foot[0]=ht_joint_at(x+f*step,feet-lift);p.foot[1]=ht_joint_at(x-f*step,feet-(3-lift/2));
    } else if(pose==HT_CUT_POSE_WALK) {
        int q=phase%32;
        for(int i=0;i<2;++i) {
            int r=(q+i*16)%32,step=r<16?8-r:r-24;
            int lift=r<16?0:(r-16)*(32-r)/26;
            p.foot[i]=ht_joint_at(x+f*step,feet-lift);
            p.hand[i]=ht_joint_at(x-f*step/2,feet-13);
        }
        p.shoulder.x=x+2*f;
    }
    return p;
}
static void ht_cutscene_draw_actor(int x,int feet,int pose,int phase,int facing) {
    ht_person_pose p=ht_cutscene_actor_pose(x,feet,pose,phase,facing);ht_person_draw(&p);
}
static void ht_cutscene_fill(int ink) {
    for(int y=0;y<HT_H;++y) {
        ht_span(ht_scene,0,HT_W-1,y,ink);
        if((y&15)==15)ht_checkpoint();
    }
}
static void ht_cutscene_caption(unsigned caption) {
    static const char *const lines[][2]={
        {"THE FIRST LIGHT APPEARED WHERE THERE","OUGHT TO HAVE BEEN A HILL."},
        {"THREE SHORT FLASHES.","FOLLOW."},
        {"SHE TAUGHT ME THE SIGNAL","IN THE TALL GRASS."},
        {"TWO SHORT. ONE LONG.","SOMEONE IS WATCHING."},
        {"SHE HAD BEEN GONE","ELEVEN DAYS."},
        {"BEFORE DAWN","I TOOK THE ROAD."},
        {"THE ROAD LOOSENED","INTO ROOTS."},
    };
    if(caption>=sizeof(lines)/sizeof(lines[0]))return;
    ht_rect(ht_scene,57,232,366,19,255);
    for(int row=0;row<2;++row) {
        const char *text=lines[caption][row];int x=(HT_W-(int)strlen(text)*6)/2;
        ht_text_color(x,233+row*10,text,1,85);
    }
}
static void ht_cutscene_draw_kitchen(const ht_cutscene_state *s,const ht_cutscene_cue *cue,int camera) {
    (void)camera;int dawn=cue->caption>=5,flash=ht_cutscene_intro_flash(s);
    ht_cutscene_fill(dawn?8:18);
    /* Window and the fog-erased country. */
    ht_rect(ht_scene,286,31,157,124,0);
    ht_rect(ht_scene,283,28,4,130,205);ht_rect(ht_scene,443,28,4,130,205);
    ht_rect(ht_scene,283,28,164,4,205);ht_rect(ht_scene,283,155,164,4,205);
    ht_rect(ht_scene,364,31,3,124,175);
    ht_rect(ht_scene,291,112,150,8,80); /* garden wall */
    ht_limb(ht_scene,318,111,304,69,2,1,88);
    ht_limb(ht_scene,331,111,348,76,2,1,72);
    ht_limb(ht_scene,404,112,423,73,2,1,66);
    ht_limb(ht_scene,348,76,359,59,1,0,55);
    if(flash) {
        int ink=ht_clamp(96+flash*159/256,0,255);
        ht_rect(ht_scene,391,61,22,3,ink);
        ht_limb(ht_scene,388,64,379,70,1,0,ink/2);
        ht_limb(ht_scene,416,64,425,70,1,0,ink/2);
    }
    /* Counter, basin, cup and shelf. */
    ht_rect(ht_scene,54,163,247,19,210);ht_rect(ht_scene,70,182,14,58,185);ht_rect(ht_scene,266,182,14,58,185);
    ht_rect(ht_scene,145,158,48,6,70);ht_disk(ht_scene,168,158,18,55);
    ht_rect(ht_scene,194,151,12,11,225);ht_limb(ht_scene,206,154,211,158,1,1,225);
    ht_limb(ht_scene,64,73,195,73,2,2,165);
    ht_rect(ht_scene,111,61,10,11,210);ht_limb(ht_scene,121,65,126,68,1,1,210);
    ht_rect(ht_scene,135,61,10,11,210);ht_limb(ht_scene,145,65,150,68,1,1,210);
    /* Table, chair, slippers and door stay mundane. */
    ht_rect(ht_scene,30,205,105,7,185);ht_rect(ht_scene,42,212,6,44,155);ht_rect(ht_scene,116,212,6,44,155);
    ht_limb(ht_scene,158,224,182,224,3,3,92);ht_limb(ht_scene,166,231,190,231,3,3,92);
    ht_rect(ht_scene,438,176,5,72,170);ht_disk(ht_scene,431,210,2,225);
    int actor=ht_cutscene_actor_x(s);
    ht_cutscene_draw_actor(actor,213,cue->pose,(int)s->tick,1);
    if(cue->pose==HT_CUT_POSE_PACK) {
        ht_rect(ht_scene,actor-14,190,14,18,230);ht_limb(ht_scene,actor-11,190,actor-4,183,1,1,210);
    }
}
static void ht_cutscene_grass_stem(int x,int base,int height,int lean,int ink) {
    ht_limb(ht_scene,x,base,x+lean,base-height,1,0,ink);
    ht_limb(ht_scene,x+lean,base-height,x+lean+4,base-height-5,1,0,ink);
}
static void ht_cutscene_draw_grass(const ht_cutscene_state *s,const ht_cutscene_cue *cue,int camera) {
    (void)camera;ht_cutscene_fill(6);
    ht_rect(ht_scene,0,205,HT_W,65,42);
    int phase=(int)s->tick/4;
    for(int layer=0;layer<3;++layer) {
        int ink=70+layer*55,base=211+layer*8,height=80+layer*25;
        for(int i=0;i<20;++i) {
            int x=(i*31+layer*17)%520-20,lean=((phase+i*7+layer*3)%11)-5;
            ht_cutscene_grass_stem(x,base,height+(i%5)*7,lean,ink);
        }
    }
    /* The distant upstairs window exists only as a small dark ambiguity. */
    if(cue->caption==3) {
        ht_rect(ht_scene,388,62,48,61,38);ht_rect(ht_scene,399,74,25,29,225);
    }
    int actor=ht_cutscene_actor_x(s);
    ht_cutscene_draw_actor(actor,213,cue->pose,(int)s->tick,1);
    ht_cutscene_draw_actor(actor+66,211,HT_CUT_POSE_CROUCH,(int)s->tick+13,-1);
    ht_limb(ht_scene,actor+69,191,actor+76,205,1,1,255); /* loose braid */
    int flash=ht_cutscene_intro_flash(s);
    if(flash) {
        int ink=ht_clamp(100+flash*155/256,0,255);
        ht_rect(ht_scene,actor+55,172,7,4,ink);
        ht_limb(ht_scene,actor+58,172,actor+71,163,1,0,ink);
    }
    /* Observer-side grass passes in front after actors. */
    for(int i=0;i<8;++i) {
        int x=(i*71+23)%520-20,lean=((phase+i*5)%13)-6;
        ht_cutscene_grass_stem(x,270,158+(i%3)*14,lean,242);
    }
}
static void ht_cutscene_draw_orchard(const ht_cutscene_state *s,const ht_cutscene_cue *cue,int camera) {
    ht_cutscene_fill(7);
    int actor_world=ht_cutscene_actor_x(s),actor=actor_world-camera;
    /* Low wet land and ditch. The maintained road gradually becomes roots. */
    ht_rect(ht_scene,0,213,HT_W,57,54);
    int wall_x=80-camera;ht_rect(ht_scene,wall_x,184,188,15,120);
    ht_rect(ht_scene,298-camera,220,92,10,18); /* ditch */
    static const int trees[]={120,245,405,575,735,910};
    for(unsigned i=0;i<sizeof(trees)/sizeof(trees[0]);++i) {
        int x=trees[i]-camera;if(x< -80 || x>HT_W+80)continue;
        int h=95+(int)(i%3)*21,w=7+(int)(i%2)*2;
        ht_organic_tree(ht_scene,x,214,h,w,700u+i*79u,170+(int)i*8,true);
    }
    int chimney=34-camera;
    if(chimney>-30) {ht_rect(ht_scene,chimney,111,14,64,150);ht_triangle(ht_scene,chimney-18,175,chimney+8,147,chimney+38,175,95);}
    /* As the road becomes roots, black forest trunks begin replacing orchard spacing. */
    if(actor_world>520) {
        for(int i=0;i<4;++i) {
            int wx=650+i*135,x=wx-camera;
            ht_limb(ht_scene,x,224,x+(i%2?8:-5),42-i*13,12,7,225-i*20);
            ht_limb(ht_scene,x-38,217,x+8,202,3,1,190);
        }
        for(int i=0;i<7;++i) {
            int wx=560+i*73,x=wx-camera;
            ht_limb(ht_scene,x,217,x+30,207-(i%3)*4,3,1,175);
        }
    }
    ht_cutscene_draw_actor(actor,213,cue->pose,(int)s->tick,1);
    /* One close alder/trunk crosses the observer plane near the handoff. */
    if(actor_world>650) {
        int x=760-camera;ht_limb(ht_scene,x,270,x-12,-10,18,13,252);
        ht_limb(ht_scene,x-8,150,x-90,103,7,3,248);
    }
}
static void ht_cutscene_render(const ht_cutscene_state *s) {
    const ht_cutscene_cue *cue=ht_cutscene_cue_at(s);if(!cue)return;
    int camera=ht_cutscene_camera_x(s);
    ht_world_scale=ht_cutscene_scale(s);
    if(cue->scene==HT_CUT_KITCHEN)ht_cutscene_draw_kitchen(s,cue,camera);
    else if(cue->scene==HT_CUT_GRASS)ht_cutscene_draw_grass(s,cue,camera);
    else ht_cutscene_draw_orchard(s,cue,camera);
    ht_world_scale=256;
    if(s->tick<55) {
        const char *title="HOLLOW TRAIL";ht_text_color((HT_W-(int)strlen(title)*12)/2,25,title,2,255);
    }
    ht_cutscene_caption(cue->caption);
    ht_vignette();
}
