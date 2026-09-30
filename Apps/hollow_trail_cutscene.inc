/* Hollow Trail cutscene timeline and tableau renderer.
 * Cutscenes use the normal game raster primitives and articulated character
 * language, but a fixed 32 ms cue track owns camera, actor pose, depth and
 * scene progression. Forward-X tableaus look down world X: screen X is world Y,
 * screen Y is world Z and world X controls perspective, fog and occlusion. */
enum { HT_CUTSCENE_NONE, HT_CUTSCENE_INTRO };
enum { HT_CUT_KITCHEN, HT_CUT_GRASS, HT_CUT_DEPARTURE, HT_CUT_ORCHARD };
enum { HT_CUT_VIEW_PROFILE, HT_CUT_VIEW_FORWARD_X };
enum { HT_CUT_FACE_FRONT, HT_CUT_FACE_BACK, HT_CUT_FACE_LEFT_3Q, HT_CUT_FACE_RIGHT_3Q };
enum { HT_CUT_POSE_IDLE, HT_CUT_POSE_WASH, HT_CUT_POSE_LOOK, HT_CUT_POSE_CROUCH,
       HT_CUT_POSE_PACK, HT_CUT_POSE_WALK };
typedef struct { int x,y,z; } ht_cut_vec3;
typedef struct {
    uint16_t start,end;
    uint8_t scene,view,pose,face,caption;
    int16_t actor_x_from,actor_x_to,actor_y_from,actor_y_to,actor_z;
    int16_t camera_x_from,camera_x_to,camera_y,camera_z;
    uint16_t framing_from,framing_to;
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
typedef struct {
    uint8_t view;
    int x,y,z;
    uint16_t framing;
    int focal,horizon,sky;
} ht_cutscene_camera;
typedef struct { int x,y,scale,depth; } ht_cutscene_projection;

static const ht_cutscene_cue ht_intro_cues[]={
    {  0, 90,HT_CUT_KITCHEN,  HT_CUT_VIEW_FORWARD_X,HT_CUT_POSE_WASH,  HT_CUT_FACE_BACK,    0,130,130,  0,  0,0,  0,  0,0,72,270,270},
    { 90,175,HT_CUT_KITCHEN,  HT_CUT_VIEW_FORWARD_X,HT_CUT_POSE_LOOK,  HT_CUT_FACE_BACK,    1,130,130,  0,  0,0,  0,  0,0,72,270,286},
    {175,305,HT_CUT_GRASS,    HT_CUT_VIEW_FORWARD_X,HT_CUT_POSE_CROUCH,HT_CUT_FACE_BACK,    2, 82, 82,-58,-58,0,  0,  0,0,48,256,256},
    {305,405,HT_CUT_GRASS,    HT_CUT_VIEW_FORWARD_X,HT_CUT_POSE_LOOK,  HT_CUT_FACE_BACK,    3, 82, 82,-58,-58,0,  0,  0,0,48,256,266},
    {405,500,HT_CUT_KITCHEN,  HT_CUT_VIEW_FORWARD_X,HT_CUT_POSE_IDLE,  HT_CUT_FACE_BACK,    4,130,130,  0,  0,0,  0,  0,0,72,284,266},
    {500,600,HT_CUT_KITCHEN,  HT_CUT_VIEW_FORWARD_X,HT_CUT_POSE_PACK,  HT_CUT_FACE_RIGHT_3Q,5,128,150,  0, 10,0,  0,  0,0,72,266,250},
    {600,730,HT_CUT_DEPARTURE,HT_CUT_VIEW_FORWARD_X,HT_CUT_POSE_WALK,  HT_CUT_FACE_BACK,  255, 90,430,  0,  0,0,  0, 18,0,66,256,244},
    {730,870,HT_CUT_ORCHARD,  HT_CUT_VIEW_PROFILE,  HT_CUT_POSE_WALK,  HT_CUT_FACE_RIGHT_3Q,255,430,650,  0,  0,0,230,450,0, 0,244,260},
    {870,930,HT_CUT_ORCHARD,  HT_CUT_VIEW_PROFILE,  HT_CUT_POSE_WALK,  HT_CUT_FACE_RIGHT_3Q,  6,650,760,  0,  0,0,450,560,0, 0,260,384},
};
#define HT_INTRO_TICKS 930u
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
    return cue?ht_cutscene_lerp(cue->actor_x_from,cue->actor_x_to,u):0;
}
static int ht_cutscene_actor_y(const ht_cutscene_state *s) {
    const ht_cutscene_cue *cue=ht_cutscene_cue_at(s);int u=ht_cutscene_progress(s,cue);
    return cue?ht_cutscene_lerp(cue->actor_y_from,cue->actor_y_to,u):0;
}
static int ht_cutscene_camera_x(const ht_cutscene_state *s) {
    const ht_cutscene_cue *cue=ht_cutscene_cue_at(s);int u=ht_cutscene_progress(s,cue);
    return cue?ht_cutscene_lerp(cue->camera_x_from,cue->camera_x_to,u):0;
}
static int ht_cutscene_scale(const ht_cutscene_state *s) {
    const ht_cutscene_cue *cue=ht_cutscene_cue_at(s);int u=ht_cutscene_progress(s,cue);
    return cue?ht_cutscene_lerp(cue->framing_from,cue->framing_to,u):256;
}
static ht_cutscene_camera ht_cutscene_camera_at(const ht_cutscene_state *s) {
    const ht_cutscene_cue *cue=ht_cutscene_cue_at(s);
    ht_cutscene_camera camera={0};
    if(!cue)return camera;
    camera.view=cue->view;camera.x=ht_cutscene_camera_x(s);camera.y=cue->camera_y;camera.z=cue->camera_z;
    camera.framing=(uint16_t)ht_cutscene_scale(s);camera.focal=420;camera.horizon=148;camera.sky=8;
    if(cue->scene==HT_CUT_GRASS){camera.horizon=138;camera.sky=6;}
    if(cue->scene==HT_CUT_DEPARTURE){camera.horizon=145;camera.sky=7;}
    return camera;
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

/* Forward-X projection. Positive X is farther from the observer. */
static ht_cutscene_projection ht_cutscene_project_forward(const ht_cutscene_camera *camera,ht_cut_vec3 p) {
    int depth=ht_clamp(p.x-camera->x,20,1600);
    int perspective=camera->focal*256/(camera->focal+depth);
    int scale=perspective*camera->framing/256;
    ht_cutscene_projection out={
        240+(p.y-camera->y)*scale/256,
        camera->horizon-(p.z-camera->z)*scale/256,
        scale,depth
    };
    return out;
}
static int ht_cutscene_depth_tone(const ht_cutscene_camera *camera,int depth,int ink) {
    int fog=ht_clamp((depth-140)*188/900,0,188);
    return camera->sky+(ink-camera->sky)*(256-fog)/256;
}
static void ht_cutscene_forward_rect(const ht_cutscene_camera *camera,ht_cut_vec3 p,int width,int height,int ink) {
    ht_cutscene_projection q=ht_cutscene_project_forward(camera,p);
    int w=ht_max(1,width*q.scale/256),h=ht_max(1,height*q.scale/256);
    int tone=ht_cutscene_depth_tone(camera,q.depth,ink);
    ht_rect(ht_scene,q.x-w/2,q.y-h,w,h,tone);
}
static void ht_cutscene_forward_disk(const ht_cutscene_camera *camera,ht_cut_vec3 p,int radius,int ink) {
    ht_cutscene_projection q=ht_cutscene_project_forward(camera,p);
    int r=ht_max(1,radius*q.scale/256),tone=ht_cutscene_depth_tone(camera,q.depth,ink);
    ht_disk(ht_scene,q.x,q.y,r,tone);
}
static void ht_cutscene_forward_limb(const ht_cutscene_camera *camera,ht_cut_vec3 a,ht_cut_vec3 b,int radius,int ink) {
    ht_cutscene_projection p=ht_cutscene_project_forward(camera,a),q=ht_cutscene_project_forward(camera,b);
    int r=ht_max(0,radius*(p.scale+q.scale)/512);
    int depth=(p.depth+q.depth)/2,tone=ht_cutscene_depth_tone(camera,depth,ink);
    ht_limb(ht_scene,p.x,p.y,q.x,q.y,r,ht_max(0,r-1),tone);
}

/* Small fixed render queue: far objects first, then near actors/grass naturally
 * occlude them. It is static BSS, not a frame allocation. */
enum { HT_CUT_OBJ_RECT, HT_CUT_OBJ_STEM, HT_CUT_OBJ_TREE, HT_CUT_OBJ_ACTOR, HT_CUT_OBJ_FLASH };
typedef struct {
    uint8_t type,pose,face,ink;
    ht_cut_vec3 p;
    int16_t a,b,c;
    uint16_t seed;
} ht_cut_object;
#define HT_CUT_OBJECTS_MAX 96
static ht_cut_object ht_cut_objects[HT_CUT_OBJECTS_MAX];
static unsigned ht_cut_object_count;
static void ht_cutscene_objects_reset(void) {ht_cut_object_count=0;}
static void ht_cutscene_object_push(ht_cut_object object) {
    if(ht_cut_object_count<HT_CUT_OBJECTS_MAX)ht_cut_objects[ht_cut_object_count++]=object;
}
static void ht_cutscene_sort_objects(void) {
    for(unsigned i=1;i<ht_cut_object_count;++i) {
        ht_cut_object key=ht_cut_objects[i];unsigned j=i;
        while(j && ht_cut_objects[j-1].p.x<key.p.x){ht_cut_objects[j]=ht_cut_objects[j-1];--j;}
        ht_cut_objects[j]=key;
    }
}
static void ht_cutscene_forward_tree(const ht_cutscene_camera *camera,const ht_cut_object *o) {
    ht_cut_vec3 base=o->p,top={o->p.x,o->p.y,o->p.z+o->a};
    int ink=o->ink;
    ht_cutscene_forward_limb(camera,base,top,o->b,ink);
    for(int side=-1;side<=1;side+=2)for(int branch=0;branch<3;++branch) {
        int z=o->p.z+o->a*(55+branch*13)/100;
        ht_cut_vec3 root={o->p.x,o->p.y,z};
        ht_cut_vec3 tip={o->p.x,o->p.y+side*(o->a/4+branch*7),z+o->a/8-branch*3};
        ht_cutscene_forward_limb(camera,root,tip,ht_max(1,o->b/3),ink-8);
        ht_cutscene_forward_disk(camera,tip,ht_max(5,o->a/12),ink-14);
    }
}
static void ht_cutscene_forward_actor(const ht_cutscene_camera *camera,const ht_cut_object *o) {
    int phase=o->a&31;
    int height=o->pose==HT_CUT_POSE_CROUCH?31:44;
    ht_cut_vec3 hip={o->p.x,o->p.y,o->p.z+17};
    ht_cut_vec3 shoulder={o->p.x,o->p.y,o->p.z+31};
    ht_cut_vec3 head={o->p.x,o->p.y,o->p.z+height};
    int quarter=o->face==HT_CUT_FACE_LEFT_3Q?-1:o->face==HT_CUT_FACE_RIGHT_3Q?1:0;
    shoulder.y+=quarter*2;head.y+=quarter*2;
    int step=0,lift0=0,lift1=0;
    if(o->pose==HT_CUT_POSE_WALK) {
        step=phase<16?7-phase:phase-23;
        lift0=phase<16?0:(phase-16)*(32-phase)/28;
        int other=(phase+16)&31;lift1=other<16?0:(other-16)*(32-other)/28;
    }
    ht_cut_vec3 foot0={o->p.x,o->p.y-5+step,o->p.z+lift0};
    ht_cut_vec3 foot1={o->p.x,o->p.y+5-step,o->p.z+lift1};
    ht_cut_vec3 hand0={o->p.x,o->p.y-9,o->p.z+22};
    ht_cut_vec3 hand1={o->p.x,o->p.y+9,o->p.z+22};
    if(o->pose==HT_CUT_POSE_WASH) {
        hand0=(ht_cut_vec3){o->p.x+12,o->p.y-6,o->p.z+25};
        hand1=(ht_cut_vec3){o->p.x+12,o->p.y+6,o->p.z+23};
    } else if(o->pose==HT_CUT_POSE_LOOK) {
        hand0.z+=3;hand1.z-=2;head.y+=quarter?quarter*2:2;
    } else if(o->pose==HT_CUT_POSE_CROUCH) {
        hip.z-=7;shoulder.z-=9;head.z-=10;
        foot0.y-=4;foot1.y+=4;hand0.z-=9;hand1.z-=9;
    } else if(o->pose==HT_CUT_POSE_PACK) {
        hand0=(ht_cut_vec3){o->p.x+5,o->p.y-8,o->p.z+19};
        hand1=(ht_cut_vec3){o->p.x-2,o->p.y+5,o->p.z+16};
        shoulder.y+=3;
    } else if(o->pose==HT_CUT_POSE_WALK) {
        hand0.y+=step/2;hand1.y-=step/2;
    }
    ht_cutscene_forward_limb(camera,hip,foot0,2,235);
    ht_cutscene_forward_limb(camera,hip,foot1,2,255);
    ht_cutscene_forward_limb(camera,shoulder,hand0,2,235);
    ht_cutscene_forward_limb(camera,shoulder,hand1,2,255);
    ht_cutscene_projection hs=ht_cutscene_project_forward(camera,shoulder),hp=ht_cutscene_project_forward(camera,hip);
    int torso=ht_cutscene_depth_tone(camera,hs.depth,250);
    int half=ht_max(2,4*hs.scale/256);
    ht_triangle(ht_scene,hs.x-half,hs.y,hs.x+half,hs.y,hp.x+half,hp.y,torso);
    ht_triangle(ht_scene,hs.x-half,hs.y,hp.x-half,hp.y,hp.x+half,hp.y,torso);
    ht_cutscene_forward_disk(camera,head,5,255);
    if(o->face==HT_CUT_FACE_BACK) {
        ht_cut_vec3 braid0={o->p.x,o->p.y+2,o->p.z+40},braid1={o->p.x,o->p.y+5,o->p.z+27};
        ht_cutscene_forward_limb(camera,braid0,braid1,1,240);
    } else if(o->face==HT_CUT_FACE_LEFT_3Q || o->face==HT_CUT_FACE_RIGHT_3Q) {
        ht_cut_vec3 nose=head;nose.y+=quarter*5;nose.z-=2;
        ht_cutscene_forward_disk(camera,nose,1,210);
    } else {
        ht_cut_vec3 face=head;face.x-=1;face.z-=1;
        ht_cutscene_forward_disk(camera,face,3,218);
    }
}
static void ht_cutscene_render_objects(const ht_cutscene_camera *camera) {
    ht_cutscene_sort_objects();
    for(unsigned i=0;i<ht_cut_object_count;++i) {
        const ht_cut_object *o=&ht_cut_objects[i];
        if(o->type==HT_CUT_OBJ_RECT)ht_cutscene_forward_rect(camera,o->p,o->a,o->b,o->ink);
        else if(o->type==HT_CUT_OBJ_STEM) {
            ht_cut_vec3 tip={o->p.x,o->p.y+o->b,o->p.z+o->a};
            ht_cutscene_forward_limb(camera,o->p,tip,1,o->ink);
            ht_cutscene_forward_limb(camera,tip,(ht_cut_vec3){tip.x,tip.y+4,tip.z+5},1,o->ink);
        } else if(o->type==HT_CUT_OBJ_TREE)ht_cutscene_forward_tree(camera,o);
        else if(o->type==HT_CUT_OBJ_ACTOR)ht_cutscene_forward_actor(camera,o);
        else if(o->type==HT_CUT_OBJ_FLASH)ht_cutscene_forward_disk(camera,o->p,o->a,o->ink);
        if((i&15u)==15u)ht_checkpoint();
    }
}

/* Triangular deterministic exposure pulse. */
static int ht_cutscene_pulse(int local,int at,int width) {
    int d=ht_abs(local-at);return d>=width?0:(width-d)*256/width;
}
static int ht_cutscene_intro_flash(const ht_cutscene_state *s) {
    const ht_cutscene_cue *cue=ht_cutscene_cue_at(s);if(!cue)return 0;
    int local=(int)s->tick-cue->start,value=0;
    if(cue->scene==HT_CUT_KITCHEN && cue->caption==1) {
        value=ht_max(value,ht_cutscene_pulse(local,12,5));
        value=ht_max(value,ht_cutscene_pulse(local,38,5));
        value=ht_max(value,ht_cutscene_pulse(local,65,5));
    } else if(cue->scene==HT_CUT_GRASS && cue->caption==2) {
        value=ht_max(value,ht_cutscene_pulse(local,35,4));
        value=ht_max(value,ht_cutscene_pulse(local,70,4));
        value=ht_max(value,ht_cutscene_pulse(local,105,4));
    } else if(cue->scene==HT_CUT_GRASS && cue->caption==3) {
        value=ht_max(value,ht_cutscene_pulse(local,20,4));
        value=ht_max(value,ht_cutscene_pulse(local,48,4));
        if(local>=72 && local<92)value=256;
    }
    return value;
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
static void ht_cutscene_queue_rect(int x,int y,int z,int width,int height,int ink) {
    ht_cut_object o={HT_CUT_OBJ_RECT,0,0,(uint8_t)ink,{x,y,z},(int16_t)width,(int16_t)height,0,0};
    ht_cutscene_object_push(o);
}
static void ht_cutscene_queue_stem(int x,int y,int z,int height,int lean,int ink) {
    ht_cut_object o={HT_CUT_OBJ_STEM,0,0,(uint8_t)ink,{x,y,z},(int16_t)height,(int16_t)lean,0,0};
    ht_cutscene_object_push(o);
}
static void ht_cutscene_queue_tree(int x,int y,int z,int height,int width,unsigned seed,int ink) {
    ht_cut_object o={HT_CUT_OBJ_TREE,0,0,(uint8_t)ink,{x,y,z},(int16_t)height,(int16_t)width,0,(uint16_t)seed};
    ht_cutscene_object_push(o);
}
static void ht_cutscene_queue_actor(int x,int y,int z,int pose,int face,int phase) {
    ht_cut_object o={HT_CUT_OBJ_ACTOR,(uint8_t)pose,(uint8_t)face,255,{x,y,z},(int16_t)phase,0,0,0};
    ht_cutscene_object_push(o);
}
static void ht_cutscene_queue_flash(int x,int y,int z,int radius,int ink) {
    ht_cut_object o={HT_CUT_OBJ_FLASH,0,0,(uint8_t)ink,{x,y,z},(int16_t)radius,0,0,0};
    ht_cutscene_object_push(o);
}

static void ht_cutscene_draw_kitchen_forward(const ht_cutscene_state *s,const ht_cutscene_cue *cue) {
    ht_cutscene_camera camera=ht_cutscene_camera_at(s);
    int dawn=cue->caption>=5,flash=ht_cutscene_intro_flash(s);
    ht_world_scale=256;ht_cutscene_fill(dawn?10:18);
    ht_rect(ht_scene,0,170,HT_W,100,38); /* floor, deliberately close and plain */
    /* Bright window opening is painted before depth objects so the garden can
     * live inside it and the near frame can occlude everything behind. */
    ht_rect(ht_scene,108,28,264,145,5);
    ht_cutscene_objects_reset();
    ht_cutscene_queue_rect(520,0,22,300,35,72); /* garden wall */
    ht_cutscene_queue_tree(610,-84,0,125,7,711,92);
    ht_cutscene_queue_tree(660,65,0,138,8,733,82);
    ht_cutscene_queue_tree(735,4,0,118,6,757,62);
    if(flash) ht_cutscene_queue_flash(900,35,92,9,ht_clamp(105+flash*150/256,0,255));
    /* Window mullions and sill at one depth plane. */
    ht_cutscene_queue_rect(325,-126,55,5,150,210);
    ht_cutscene_queue_rect(325,126,55,5,150,210);
    ht_cutscene_queue_rect(325,0,55,5,150,180);
    ht_cutscene_queue_rect(325,0,55,255,5,210);
    ht_cutscene_queue_rect(325,0,190,255,5,210);
    /* Sink/counter and small domestic anchors between observer and window. */
    ht_cutscene_queue_rect(210,0,0,235,42,205);
    ht_cutscene_queue_rect(205,-72,49,42,8,75);   /* basin lip */
    ht_cutscene_queue_rect(190,-118,132,98,5,160);/* shelf */
    ht_cutscene_queue_rect(188,-100,132,13,16,222);/* sister cup */
    ht_cutscene_queue_rect(188,-68,132,13,16,218);
    /* Table/chair and slippers sit closer to the observer, therefore larger. */
    ht_cutscene_queue_rect(92,-118,0,102,28,175);
    ht_cutscene_queue_rect(75,115,0,38,42,128);
    ht_cutscene_queue_rect(58,72,0,25,6,95);
    ht_cutscene_queue_rect(58,103,0,25,6,95);
    ht_cutscene_queue_actor(ht_cutscene_actor_x(s),ht_cutscene_actor_y(s),0,cue->pose,cue->face,(int)s->tick);
    if(cue->pose==HT_CUT_POSE_PACK)ht_cutscene_queue_rect(116,18,0,25,25,212);
    ht_cutscene_render_objects(&camera);
}
static void ht_cutscene_draw_grass_forward(const ht_cutscene_state *s,const ht_cutscene_cue *cue) {
    ht_cutscene_camera camera=ht_cutscene_camera_at(s);
    int phase=(int)s->tick/3,flash=ht_cutscene_intro_flash(s);
    ht_world_scale=256;ht_cutscene_fill(6);
    ht_rect(ht_scene,0,190,HT_W,80,48);
    ht_cutscene_objects_reset();
    /* Far house/window is genuinely far down X and therefore small/light. */
    if(cue->caption==3) {
        ht_cutscene_queue_rect(980,92,0,145,96,62);
        ht_cutscene_queue_rect(970,92,72,44,46,205);
    }
    for(int layer=0;layer<5;++layer)for(int i=0;i<15;++i) {
        int depth=130+layer*130+(i%4)*24;
        int lateral=((i*71+layer*39)%360)-180;
        int height=105+(i%5)*13+layer*4;
        int lean=((phase+i*7+layer*11)%19)-9;
        ht_cutscene_queue_stem(depth,lateral,0,height,lean,82+layer*18);
    }
    /* Narrator close to the observer, sister farther inside the flattened room. */
    ht_cutscene_queue_actor(ht_cutscene_actor_x(s),ht_cutscene_actor_y(s),0,cue->pose,cue->face,(int)s->tick);
    int sister_face=cue->caption==3?HT_CUT_FACE_RIGHT_3Q:HT_CUT_FACE_FRONT;
    ht_cutscene_queue_actor(310,38,0,HT_CUT_POSE_CROUCH,sister_face,(int)s->tick+13);
    if(flash)ht_cutscene_queue_flash(306,23,36,4,ht_clamp(110+flash*145/256,0,255));
    /* Very near stems are added last by depth sort and can cross both figures. */
    for(int i=0;i<10;++i) {
        int depth=42+(i%4)*14,lateral=((i*93+27)%430)-215;
        ht_cutscene_queue_stem(depth,lateral,0,155+(i%3)*18,((phase+i*5)%17)-8,238);
    }
    ht_cutscene_render_objects(&camera);
}
static void ht_cutscene_draw_departure_forward(const ht_cutscene_state *s,const ht_cutscene_cue *cue) {
    ht_cutscene_camera camera=ht_cutscene_camera_at(s);
    int actor_x=ht_cutscene_actor_x(s);
    ht_world_scale=256;ht_cutscene_fill(7);
    ht_rect(ht_scene,0,190,HT_W,80,48);
    ht_cutscene_objects_reset();
    /* Orchard rows and walls recede down X while the doorway remains near. */
    ht_cutscene_queue_rect(520,0,18,300,28,92);
    for(int row=0;row<2;++row)for(int i=0;i<5;++i) {
        int depth=235+i*115+row*28,lateral=(row?-1:1)*(72+i*13);
        ht_cutscene_queue_tree(depth,lateral,0,104+(i%3)*17,7,801u+row*71u+i*29u,125+i*8);
    }
    ht_cutscene_queue_rect(155,-128,0,6,190,205); /* doorway edges */
    ht_cutscene_queue_rect(155,128,0,6,190,205);
    ht_cutscene_queue_rect(155,0,180,258,6,205);
    ht_cutscene_queue_rect(410,0,0,170,12,54); /* ditch shadow */
    ht_cutscene_queue_actor(actor_x,ht_cutscene_actor_y(s),0,HT_CUT_POSE_WALK,HT_CUT_FACE_BACK,(int)s->tick);
    ht_cutscene_render_objects(&camera);
}
static void ht_cutscene_draw_orchard_profile(const ht_cutscene_state *s,const ht_cutscene_cue *cue) {
    int camera=ht_cutscene_camera_x(s),actor_world=ht_cutscene_actor_x(s),actor=actor_world-camera;
    ht_world_scale=ht_cutscene_scale(s);ht_cutscene_fill(7);
    ht_rect(ht_scene,0,213,HT_W,57,54);
    int wall_x=80-camera;ht_rect(ht_scene,wall_x,184,188,15,120);
    ht_rect(ht_scene,298-camera,220,92,10,18);
    static const int trees[]={120,245,405,575,735,910};
    for(unsigned i=0;i<sizeof(trees)/sizeof(trees[0]);++i) {
        int x=trees[i]-camera;if(x< -80 || x>HT_W+80)continue;
        int h=95+(int)(i%3)*21,w=7+(int)(i%2)*2;
        ht_organic_tree(ht_scene,x,214,h,w,700u+i*79u,170+(int)i*8,true);
    }
    int chimney=34-camera;
    if(chimney>-30){ht_rect(ht_scene,chimney,111,14,64,150);ht_triangle(ht_scene,chimney-18,175,chimney+8,147,chimney+38,175,95);}
    if(actor_world>560) {
        for(int i=0;i<5;++i) {
            int wx=650+i*115,x=wx-camera;
            ht_limb(ht_scene,x,224,x+(i%2?8:-5),35-i*9,12,7,225-i*16);
            ht_limb(ht_scene,x-38,217,x+8,202,3,1,190);
        }
        for(int i=0;i<8;++i) {
            int wx=555+i*70,x=wx-camera;
            ht_limb(ht_scene,x,217,x+30,207-(i%3)*4,3,1,175);
        }
    }
    ht_cutscene_draw_actor(actor,213,cue->pose,(int)s->tick,1);
    if(actor_world>690) {
        int x=760-camera;ht_limb(ht_scene,x,270,x-12,-10,18,13,252);
        ht_limb(ht_scene,x-8,150,x-90,103,7,3,248);
    }
    ht_world_scale=256;
}
static void ht_cutscene_render(const ht_cutscene_state *s) {
    const ht_cutscene_cue *cue=ht_cutscene_cue_at(s);if(!cue)return;
    if(cue->view==HT_CUT_VIEW_FORWARD_X) {
        if(cue->scene==HT_CUT_KITCHEN)ht_cutscene_draw_kitchen_forward(s,cue);
        else if(cue->scene==HT_CUT_GRASS)ht_cutscene_draw_grass_forward(s,cue);
        else ht_cutscene_draw_departure_forward(s,cue);
    } else ht_cutscene_draw_orchard_profile(s,cue);
    ht_world_scale=256;
    if(s->tick<55) {
        const char *title="HOLLOW TRAIL";ht_text_color((HT_W-(int)strlen(title)*12)/2,25,title,2,255);
    }
    ht_cutscene_caption(cue->caption);
    ht_vignette();
}
