/* Hollow Trail cutscene timeline and tableau renderer.
 * Cutscenes use the normal game raster primitives and articulated character
 * language, but a fixed 32 ms cue track owns camera, actor pose, depth and
 * scene progression. Forward-X tableaus look down world X: screen X is world Y,
 * screen Y is world Z and world X controls perspective, fog and occlusion. */
enum { HT_CUTSCENE_NONE, HT_CUTSCENE_INTRO, HT_CUTSCENE_MILL, HT_CUTSCENE_CITY };
enum { HT_CUT_KITCHEN, HT_CUT_GRASS, HT_CUT_DEPARTURE, HT_CUT_ORCHARD, HT_CUT_MILL };
enum { HT_CUT_VIEW_PROFILE, HT_CUT_VIEW_FORWARD_X };
enum { HT_CUT_FACE_FRONT, HT_CUT_FACE_BACK, HT_CUT_FACE_LEFT_3Q, HT_CUT_FACE_RIGHT_3Q };
enum { HT_CUT_POSE_IDLE, HT_CUT_POSE_WASH, HT_CUT_POSE_LOOK, HT_CUT_POSE_CROUCH,
       HT_CUT_POSE_PACK, HT_CUT_POSE_WALK, HT_CUT_POSE_SIT, HT_CUT_POSE_THREAD };
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
    /* Give every readable image time to exist before the next idea arrives. */
    {   0, 180,HT_CUT_KITCHEN,HT_CUT_VIEW_FORWARD_X,HT_CUT_POSE_WASH,  HT_CUT_FACE_BACK,    0,130,130,  0,  0,0,  0,  0,0,72,270,270},
    { 180, 340,HT_CUT_KITCHEN,HT_CUT_VIEW_FORWARD_X,HT_CUT_POSE_LOOK,  HT_CUT_FACE_BACK,    1,130,130,  0,  0,0,  0,  0,0,72,270,286},
    { 340, 570,HT_CUT_GRASS,  HT_CUT_VIEW_FORWARD_X,HT_CUT_POSE_CROUCH,HT_CUT_FACE_BACK,    2, 82, 82,-58,-58,0,  0,  0,0,48,256,256},
    { 570, 800,HT_CUT_GRASS,  HT_CUT_VIEW_FORWARD_X,HT_CUT_POSE_LOOK,  HT_CUT_FACE_BACK,    3, 82, 82,-58,-58,0,  0,  0,0,48,256,266},
    { 800, 980,HT_CUT_KITCHEN,HT_CUT_VIEW_FORWARD_X,HT_CUT_POSE_IDLE,  HT_CUT_FACE_BACK,    4,130,130,  0,  0,0,  0,  0,0,72,284,270},
    { 980,1170,HT_CUT_KITCHEN,HT_CUT_VIEW_FORWARD_X,HT_CUT_POSE_SIT,   HT_CUT_FACE_RIGHT_3Q,7,128,128, 0,0,0, 0,0,0,72,270,270},
    {1170,1350,HT_CUT_KITCHEN,HT_CUT_VIEW_FORWARD_X,HT_CUT_POSE_PACK,  HT_CUT_FACE_RIGHT_3Q,5,128,150,  0, 10,0,  0,  0,0,72,270,270},
    {1350,1510,HT_CUT_ORCHARD,HT_CUT_VIEW_PROFILE,HT_CUT_POSE_THREAD,HT_CUT_FACE_LEFT_3Q,8,118,118,0,0,0,-40,-40,0,0,270,270},
    /* Continuity cut: profile view begins at the house, not halfway down the road. */
    {1510,1690,HT_CUT_ORCHARD,HT_CUT_VIEW_PROFILE,  HT_CUT_POSE_WALK,  HT_CUT_FACE_RIGHT_3Q,5,118,190,  0,  0,0,-40,  0,0, 0,270,270},
    {1690,2020,HT_CUT_ORCHARD,HT_CUT_VIEW_PROFILE,  HT_CUT_POSE_WALK,  HT_CUT_FACE_RIGHT_3Q,255,190,540, 0,  0,0,  0,300,0, 0,270,286},
    {2020,2280,HT_CUT_ORCHARD,HT_CUT_VIEW_PROFILE,  HT_CUT_POSE_WALK,  HT_CUT_FACE_RIGHT_3Q,6,540,760,  0,  0,0,300,560,0, 0,286,384},
};
#define HT_INTRO_TICKS 2280u
static const ht_cutscene_cue ht_mill_cues[]={
    {0,100,HT_CUT_MILL,HT_CUT_VIEW_PROFILE,HT_CUT_POSE_IDLE,0,9,0,0,0,0,0,0,24,0,0,384,272},
    {100,240,HT_CUT_MILL,HT_CUT_VIEW_PROFILE,HT_CUT_POSE_IDLE,0,10,0,0,0,0,0,24,24,0,0,272,272},
    {240,340,HT_CUT_MILL,HT_CUT_VIEW_PROFILE,HT_CUT_POSE_IDLE,0,255,0,0,0,0,0,24,0,0,0,272,384},
};
static const ht_cutscene_cue ht_city_cues[]={
    {0,160,HT_CUT_MILL,HT_CUT_VIEW_PROFILE,HT_CUT_POSE_IDLE,0,11,0,0,0,0,0,0,120,0,0,384,240},
    {160,330,HT_CUT_MILL,HT_CUT_VIEW_PROFILE,HT_CUT_POSE_IDLE,0,12,0,0,0,0,0,120,120,0,0,240,240},
    {330,430,HT_CUT_MILL,HT_CUT_VIEW_PROFILE,HT_CUT_POSE_IDLE,0,255,0,0,0,0,0,120,0,0,0,240,384},
};
static const ht_cutscene_timeline ht_cutscene_tracks[]={
    {NULL,0,0,0},
    {ht_intro_cues,(uint8_t)(sizeof(ht_intro_cues)/sizeof(ht_intro_cues[0])),0,HT_INTRO_TICKS},
    {ht_mill_cues,3,0,340},
    {ht_city_cues,3,1,430}
};
static ht_cutscene_state ht_cutscene;
static ht_game ht_cutscene_anchor;
static uint8_t ht_cutscene_seen; /* Session-local: a return across the mill cannot replay it. */
static bool ht_cutscene_mill_arrival(const ht_game *before,const ht_game *after) {
    return !(ht_cutscene_seen&1u) && before->level==0 && after->level==0 &&
        after->x>before->x && after->x>=1070*256 && after->x<1150*256 && after->grounded &&
        after->traversal.mode==HT_FREE;
}

static bool ht_cutscene_city_arrival(const ht_game *before,const ht_game *after) {
    return !(ht_cutscene_seen&2u) && before->level==0 && after->level==1 &&
        before->x>=(HT_GOAL-8)*256 && before->puzzle.solved && before->puzzle.opening==48;
}
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
    ht_cutscene_anchor=ht;
    if(id==HT_CUTSCENE_MILL)ht_cutscene_seen|=1u;
    if(id==HT_CUTSCENE_CITY)ht_cutscene_seen|=2u;
}
static bool ht_cutscene_step(ht_cutscene_state *s) {
    if(!s->active)return false;
    const ht_cutscene_timeline *track=ht_cutscene_track(s);
    if(++s->tick<track->duration)return false;
    s->tick=track->duration;s->active=false;s->finished=true;return true;
}
static void ht_cutscene_apply_handoff(const ht_cutscene_state *s) {
    if(s->id==HT_CUTSCENE_INTRO) {
        const ht_cutscene_timeline *track=ht_cutscene_track(s);
        ht.level=track->handoff_level;ht_spawn(true);
    } /* A location tableau returns to its untouched game state, never respawns. */
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
static void ht_cutscene_forward_quad(const ht_cutscene_camera *camera,
    ht_cut_vec3 a,ht_cut_vec3 b,ht_cut_vec3 c,ht_cut_vec3 d,int ink) {
    ht_cutscene_projection pa=ht_cutscene_project_forward(camera,a),pb=ht_cutscene_project_forward(camera,b);
    ht_cutscene_projection pc=ht_cutscene_project_forward(camera,c),pd=ht_cutscene_project_forward(camera,d);
    int depth=(pa.depth+pb.depth+pc.depth+pd.depth)/4,tone=ht_cutscene_depth_tone(camera,depth,ink);
    ht_triangle(ht_scene,pa.x,pa.y,pb.x,pb.y,pc.x,pc.y,tone);
    ht_triangle(ht_scene,pa.x,pa.y,pc.x,pc.y,pd.x,pd.y,tone);
}
static void ht_cutscene_forward_box(const ht_cutscene_camera *camera,ht_cut_vec3 p,
    int depth,int width,int height,int ink) {
    int near=p.x-depth/2,far=p.x+depth/2,left=p.y-width/2,right=p.y+width/2,top=p.z+height;
    ht_cut_vec3 f0={far,left,p.z},f1={far,right,p.z},f2={far,right,top},f3={far,left,top};
    ht_cut_vec3 n0={near,left,p.z},n1={near,right,p.z},n2={near,right,top},n3={near,left,top};
    /* Back, side and top planes give furniture/buildings volume; front is last. */
    ht_cutscene_forward_quad(camera,f0,f1,f2,f3,ht_max(0,ink-28));
    ht_cutscene_forward_quad(camera,f1,n1,n2,f2,ht_max(0,ink-12));
    ht_cutscene_forward_quad(camera,f3,f2,n2,n3,ht_min(255,ink+14));
    ht_cutscene_forward_quad(camera,n0,n1,n2,n3,ink);
}

/* Small fixed render queue: far objects first, then near actors/grass naturally
 * occlude them. It is static BSS, not a frame allocation. */
enum { HT_CUT_OBJ_RECT, HT_CUT_OBJ_STEM, HT_CUT_OBJ_TREE, HT_CUT_OBJ_ACTOR, HT_CUT_OBJ_FLASH, HT_CUT_OBJ_BOX };
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
    int h=o->a,w=ht_max(5,(int)o->b),ink=o->ink;
    /* Filled tapering trunk with a flared root collar instead of one wire-like limb. */
    ht_cut_vec3 bl={o->p.x,o->p.y-w,o->p.z},br={o->p.x,o->p.y+w,o->p.z};
    ht_cut_vec3 tl={o->p.x,o->p.y-w/2,o->p.z+h},tr={o->p.x,o->p.y+w/2,o->p.z+h};
    ht_cutscene_forward_quad(camera,bl,br,tr,tl,ink);
    ht_cutscene_forward_quad(camera,
        (ht_cut_vec3){o->p.x,o->p.y-w*2,o->p.z},
        (ht_cut_vec3){o->p.x,o->p.y+w*2,o->p.z},
        (ht_cut_vec3){o->p.x,o->p.y+w,o->p.z+h/7},
        (ht_cut_vec3){o->p.x,o->p.y-w,o->p.z+h/7},ht_min(255,ink+12));
    /* Bark seams follow growth and remain subordinate to the silhouette. */
    for(int seam=-1;seam<=1;++seam)
        ht_cutscene_forward_limb(camera,
            (ht_cut_vec3){o->p.x-1,o->p.y+seam*w/2,o->p.z+h/10},
            (ht_cut_vec3){o->p.x,o->p.y+seam*w/3,o->p.z+h*8/10},1,ht_max(0,ink-32));
    for(int side=-1;side<=1;side+=2)for(int branch=0;branch<4;++branch) {
        int z=o->p.z+h*(48+branch*11)/100,reach=h/5+branch*5;
        ht_cut_vec3 root={o->p.x,o->p.y+side*w/3,z};
        ht_cut_vec3 mid={o->p.x,o->p.y+side*(reach*2/3),z+h/11};
        ht_cut_vec3 tip={o->p.x,o->p.y+side*reach,z+h/8-branch*2};
        ht_cutscene_forward_limb(camera,root,mid,ht_max(2,w/3),ht_max(0,ink-5));
        ht_cutscene_forward_limb(camera,mid,tip,ht_max(1,w/5),ht_max(0,ink-12));
        /* Irregular clustered crown: several overlapping filled masses, not one disk. */
        ht_cutscene_forward_disk(camera,tip,ht_max(7,h/13),ht_max(0,ink-18));
        ht_cutscene_forward_disk(camera,(ht_cut_vec3){tip.x,tip.y-side*h/20,tip.z+h/28},
            ht_max(5,h/17),ht_max(0,ink-27));
        ht_cutscene_forward_disk(camera,(ht_cut_vec3){tip.x,tip.y+side*h/24,tip.z-h/35},
            ht_max(5,h/18),ht_max(0,ink-23));
    }
}

static void ht_cutscene_forward_actor(const ht_cutscene_camera *camera,const ht_cut_object *o) {
    /* Human scale: the old 44-unit figure was shorter than the sink. Preserve
     * its world-space foot, but render the articulated body at domestic scale. */
    ht_cutscene_projection foot=ht_cutscene_project_forward(camera,o->p);
    ht_cutscene_camera human=*camera;
    human.framing=(uint16_t)(camera->framing*12/5);
    int scale=(camera->focal*256/(camera->focal+foot.depth))*human.framing/256;
    human.y=o->p.y-(foot.x-240)*256/ht_max(1,scale);
    human.z=o->p.z;human.horizon=foot.y;camera=&human;
    int phase=o->a&31,height=o->pose==HT_CUT_POSE_CROUCH?38:44;
    ht_cut_vec3 hip={o->p.x,o->p.y,o->p.z+17},shoulder={o->p.x,o->p.y,o->p.z+31};
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
    ht_cut_vec3 hand0={o->p.x,o->p.y-9,o->p.z+22},hand1={o->p.x,o->p.y+9,o->p.z+22};
    if(o->pose==HT_CUT_POSE_WASH) {
        hand0=(ht_cut_vec3){o->p.x+12,o->p.y-6,o->p.z+25}; hand1=(ht_cut_vec3){o->p.x+12,o->p.y+6,o->p.z+23};
    } else if(o->pose==HT_CUT_POSE_LOOK) {
        hand0.z+=3;hand1.z-=2;head.y+=quarter?quarter*2:2;
    } else if(o->pose==HT_CUT_POSE_CROUCH) {
        hip.z-=7;shoulder.z-=9;head.z-=10;foot0.y-=4;foot1.y+=4;hand0.z-=9;hand1.z-=9;
    } else if(o->pose==HT_CUT_POSE_PACK) {
        hand0=(ht_cut_vec3){o->p.x+5,o->p.y-8,o->p.z+19}; hand1=(ht_cut_vec3){o->p.x-2,o->p.y+5,o->p.z+16}; shoulder.y+=3;
    } else if(o->pose==HT_CUT_POSE_WALK) { hand0.y+=step/2;hand1.y-=step/2; }

    /* Weightier limbs and a filled coat body eliminate the stick/wireframe read. */
    if(o->pose==HT_CUT_POSE_CROUCH) {
        ht_cut_vec3 knee0={hip.x,hip.y-8,hip.z-3},knee1={hip.x,hip.y+8,hip.z-3};
        foot0.y=hip.y-4;foot1.y=hip.y+4;
        ht_cutscene_forward_limb(camera,hip,knee0,3,224);ht_cutscene_forward_limb(camera,knee0,foot0,2,224);
        ht_cutscene_forward_limb(camera,hip,knee1,3,248);ht_cutscene_forward_limb(camera,knee1,foot1,2,248);
    } else {
        ht_cutscene_forward_limb(camera,hip,foot0,3,224);ht_cutscene_forward_limb(camera,hip,foot1,3,248);
    }
    ht_cutscene_forward_limb(camera,shoulder,hand0,3,224); ht_cutscene_forward_limb(camera,shoulder,hand1,3,248);
    ht_cutscene_projection hs=ht_cutscene_project_forward(camera,shoulder),hp=ht_cutscene_project_forward(camera,hip);
    int tone=ht_cutscene_depth_tone(camera,hs.depth,248);
    int sh=ht_max(3,7*hs.scale/256),waist=ht_max(3,5*hp.scale/256),hem=ht_max(4,8*hp.scale/256);
    int hem_y=hp.y+ht_max(2,5*hp.scale/256);
    ht_triangle(ht_scene,hs.x-sh,hs.y,hs.x+sh,hs.y,hp.x+waist,hp.y,tone);
    ht_triangle(ht_scene,hs.x-sh,hs.y,hp.x-waist,hp.y,hp.x+waist,hp.y,tone);
    ht_triangle(ht_scene,hp.x-waist,hp.y,hp.x+waist,hp.y,hp.x+hem,hem_y,ht_max(0,tone-7));
    ht_triangle(ht_scene,hp.x-waist,hp.y,hp.x+hem,hem_y,hp.x-hem,hem_y,ht_max(0,tone-7));
    /* Head gets a hair mass plus face plane rather than a single white dot. */
    ht_cutscene_forward_disk(camera,(ht_cut_vec3){head.x+1,head.y,head.z+1},6,225);
    ht_cutscene_forward_disk(camera,head,5,252);
    if(o->face==HT_CUT_FACE_BACK) {
        ht_cutscene_forward_disk(camera,(ht_cut_vec3){head.x-1,head.y,head.z+2},6,205);
        ht_cutscene_forward_limb(camera,(ht_cut_vec3){o->p.x,o->p.y+2,o->p.z+40},
            (ht_cut_vec3){o->p.x,o->p.y+5,o->p.z+27},2,216);
        ht_cutscene_forward_limb(camera,(ht_cut_vec3){o->p.x,o->p.y+5,o->p.z+28},
            (ht_cut_vec3){o->p.x,o->p.y+2,o->p.z+19},1,190);
    } else if(quarter) {
        ht_cut_vec3 nose=head;nose.y+=quarter*5;nose.z-=2;ht_cutscene_forward_disk(camera,nose,1,188);
    } else {
        ht_cutscene_forward_limb(camera,(ht_cut_vec3){head.x-1,head.y-2,head.z+1},
            (ht_cut_vec3){head.x-1,head.y+2,head.z+1},1,202);
    }
    /* Jacket center seam remains subtle at useful close scale. */
    ht_cutscene_forward_limb(camera,(ht_cut_vec3){hip.x,hip.y,hip.z+1},(ht_cut_vec3){shoulder.x,shoulder.y,shoulder.z-2},1,190);
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
        else if(o->type==HT_CUT_OBJ_BOX)ht_cutscene_forward_box(camera,o->p,o->a,o->b,o->c,o->ink);
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
        value=ht_max(value,ht_cutscene_pulse(local,35,7));
        value=ht_max(value,ht_cutscene_pulse(local,80,7));
        value=ht_max(value,ht_cutscene_pulse(local,125,7));
    } else if(cue->scene==HT_CUT_GRASS && cue->caption==2) {
        value=ht_max(value,ht_cutscene_pulse(local,55,6));
        value=ht_max(value,ht_cutscene_pulse(local,110,6));
        value=ht_max(value,ht_cutscene_pulse(local,165,6));
    } else if(cue->scene==HT_CUT_GRASS && cue->caption==3) {
        value=ht_max(value,ht_cutscene_pulse(local,45,6));
        value=ht_max(value,ht_cutscene_pulse(local,100,6));
        if(local>=145 && local<195)value=256;
    }
    return value;
}
static void ht_cutscene_fill(int ink) {
    for(int y=0;y<HT_H;++y) {
        ht_rect(ht_scene,0,y,HT_W,1,ink);
        if((y&15)==15)ht_checkpoint();
    }
}
static void ht_cutscene_profile_actor(int x,int feet,int pose,int phase,int facing) {
    ht_game actor=ht;
    actor.facing=facing;actor.grounded=true;actor.traversal.mode=HT_FREE;actor.traversal.support=1;
    actor.vx=pose==HT_CUT_POSE_WALK?320:0;actor.stride=phase*128;
    ht_person_pose p=ht_person_pose_at(x,feet,&actor);ht_person_draw(&p);
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
        {"I SPENT THE NIGHT DRESSED IN A CHAIR.","THE CUP REMAINED CLOUDY WITH SOAP."},
        {"MY COAT CAUGHT ON THE LATCH.","I FREED THE THREAD."},
        {"THE MILL STOOD IN A DIP BETWEEN HILLS.","ITS WHEEL WAS STILL BENEATH THE LEAVES."},
        {"A BROKEN SHUTTER. A SLOPING DESK.","SEVEN NAMES IN THE WINDOW LIGHT."},
        {"UPPER FLOORS ROSE FROM THE FOG.","THE HILLS CONTINUED UNDER THE CITY."},
        {"THE CITY BEGAN OVERHEAD.","NO STREETS JOINED THESE ROOFS."},
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
static void ht_cutscene_queue_box(int x,int y,int z,int depth,int width,int height,int ink) {
    ht_cut_object o={HT_CUT_OBJ_BOX,0,0,(uint8_t)ink,{x,y,z},(int16_t)depth,(int16_t)width,(int16_t)height,0};
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

#include "hollow_trail_domestic_draw.inc"

static void ht_cutscene_draw_departure_forward(const ht_cutscene_state *s,const ht_cutscene_cue *cue) {
    (void)cue;
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
    ht_world_scale=ht_cutscene_scale(s);ht_cutscene_fill(8);
    /* Continuous ground from doorstep to root-soft road. */
    ht_rect(ht_scene,0,213,HT_W,57,58);
    for(int x=-40;x<HT_W+40;x+=37)ht_limb(ht_scene,x,214,x+24,210+(x/37&1)*4,1,0,96);

    /* The profile grammar begins at the house. Wall segments leave a real doorway gap. */
    int hx=-80-camera,door=118-camera;
    if(hx<HT_W+80) {
        ht_rect(ht_scene,hx,99,195,114,158);
        ht_triangle(ht_scene,hx-12,100,hx+93,52,hx+210,100,126);
        ht_rect(ht_scene,hx+18,72,18,54,118); /* chimney */
        ht_rect(ht_scene,door-22,140,17,73,82);
        ht_rect(ht_scene,door+16,140,49,73,82); /* split wall around door */
        ht_rect(ht_scene,door-5,140,21,73,24);  /* dark open interior */
        /* Open door leaf rotates visually into a thin offset slab as she emerges. */
        int open=ht_clamp((actor_world-112)*2,0,24);
        ht_rect(ht_scene,door+15+open,143,5,68,132);
        ht_rect(ht_scene,hx+67,117,31,29,205);ht_rect(ht_scene,hx+71,121,23,21,48);
        ht_limb(ht_scene,hx+4,101,hx+194,101,2,2,98); /* eave */
    }
    int wall_x=188-camera;ht_rect(ht_scene,wall_x,187,185,12,116);
    ht_rect(ht_scene,310-camera,220,92,10,22); /* ditch */

    /* Orchard trees are world anchored and drawn with generous off-screen extent. */
    static const int orchard_trees[]={205,330,465,605,755,920};
    for(unsigned i=0;i<sizeof(orchard_trees)/sizeof(orchard_trees[0]);++i) {
        int x=orchard_trees[i]-camera,h=100+(int)(i%3)*22,w=8+(int)(i%2)*3;
        int extent=h+45;
        if(x< -extent || x>HT_W+extent)continue;
        ht_organic_tree(ht_scene,x,214,h,w,700u+i*79u,165+(int)i*8,true);
    }
    int chimney=5-camera;
    if(chimney>-45) {ht_rect(ht_scene,chimney,82,13,60,146);}

    /* Forest approach is always present in world space. No actor-position gate:
     * trunks and roots are rasterized before their crowns can enter the viewport. */
    for(int i=0;i<6;++i) {
        int wx=610+i*112,x=wx-camera,h=190+i*17,extent=h/2+85;
        if(x< -extent || x>HT_W+extent)continue;
        ht_organic_tree(ht_scene,x,224,h,13+(i%2)*4,1401u+i*83u,210-i*13,true);
    }
    for(int i=0;i<11;++i) {
        int wx=525+i*66,x=wx-camera;
        if(x< -120 || x>HT_W+120)continue;
        ht_limb(ht_scene,x,217,x+34,207-(i%3)*5,4,1,166);
        ht_limb(ht_scene,x+3,216,x-17,210+(i%2)*4,2,0,142);
    }

    /* Novella departure: a taut coat thread is freed before the first step.
     * The close hold is an action, not a caption over an already walking actor. */
    if(cue->pose==HT_CUT_POSE_THREAD) {
        int local=(int)s->tick-cue->start;
        int reach=ht_clamp(local<90?local:160-local,0,60)/6;
        ht_limb(ht_scene,actor-1,197,door+17,184,0,0,local<100?190:24);
        ht_limb(ht_scene,actor,198,actor+reach,191,1,1,235);
        ht_disk(ht_scene,door+17,184,2,171);
        /* Lit kitchen beyond: chair tucked beneath the table, crossed slippers. */
        ht_limb(ht_scene,door-17,205,door-7,205,1,1,90);
        ht_limb(ht_scene,door-15,199,door-15,212,1,1,90);
    }
    /* Actor draws after terrain but before the closest observer trunk. */
    ht_cutscene_profile_actor(actor,213,cue->pose,(int)s->tick,1);
    ht_limb(ht_scene,actor-4,198,actor+5,205,1,1,150);
    ht_rect(ht_scene,actor+3,204,6,7,202); /* packed shoulder bag */
    int observer_x=760-camera;
    if(observer_x>-150 && observer_x<HT_W+150) {
        ht_limb(ht_scene,observer_x,270,observer_x-12,-10,18,13,252);
        ht_limb(ht_scene,observer_x-8,150,observer_x-90,103,7,3,248);
    }
    ht_world_scale=256;
}

static void ht_cutscene_draw_arrival(const ht_cutscene_state *s) {
    ht_game view=ht_cutscene_anchor;
    view.camera+=ht_cutscene_camera_x(s)*256;
    int scale=ht_cutscene_scale(s);
    view.intimacy=(uint16_t)ht_clamp((scale-256)*2,0,256);
    view.vista=(uint16_t)(scale<256?(256-scale)*16/5:0);view.sway_phase=0;view.rotation_phase=0;view.vx=0;
    ht_world_scale=ht_scene_scale(&view);
    ht_draw_traversal(&view);
    ht_character(view.x/256-view.camera/256,(view.y-view.camera_y)/256,&view);
    ht_forest_foreground(&view);ht_city_observer_foreground(&view);
    ht_world_scale=256;ht_native_foreground_half_y=false;
}
static void ht_cutscene_render(const ht_cutscene_state *s) {
    const ht_cutscene_cue *cue=ht_cutscene_cue_at(s);if(!cue)return;
    /* Match the selected output raster; later tableaus can start after native
     * gameplay. Never leave a native-size buffer paired with low-size clearing. */
    ht_native_active=ht_camera_mode==HT_CAMERA_NATIVE && ht_native_a;
    ht_native_foreground_half_y=false;
    ht_scene=ht_native_active?ht_native_a:ht_scene_low;
    if(s->id==HT_CUTSCENE_MILL || s->id==HT_CUTSCENE_CITY)ht_cutscene_draw_arrival(s);
    else if(cue->pose==HT_CUT_POSE_THREAD)ht_cutscene_draw_latch(s,cue);
    else if(cue->view==HT_CUT_VIEW_FORWARD_X) {
        if(cue->scene==HT_CUT_KITCHEN)ht_cutscene_draw_kitchen_forward(s,cue);
        else if(cue->scene==HT_CUT_GRASS)ht_cutscene_draw_grass_forward(s,cue);
        else ht_cutscene_draw_departure_forward(s,cue);
    } else ht_cutscene_draw_orchard_profile(s,cue);
    ht_world_scale=256;
    if(s->id==HT_CUTSCENE_INTRO && s->tick<55) {
        const char *title="HOLLOW TRAIL";ht_text_color((HT_W-(int)strlen(title)*12)/2,25,title,2,255);
    }
    ht_cutscene_caption(cue->caption);
    ht_vignette();
}
