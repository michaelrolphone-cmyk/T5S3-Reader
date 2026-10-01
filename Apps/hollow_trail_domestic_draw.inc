/* Novella I: cold cup, absent sister, a night of waiting and a deliberate
 * departure. Small authored silhouettes carry the story; no textures, new
 * buffers, random shimmer or physical display changes. Ink 0 is paper white. */
static void ht_cut_cup(int x,int y,int scale,bool chipped,int ink) {
    int r=scale;
    ht_disk(ht_scene,x+r+2,y-r,ht_max(2,r/2),ink);
    ht_disk(ht_scene,x+r+2,y-r,ht_max(1,r/2-1),65);
    ht_triangle(ht_scene,x-r,y-2*r,x+r,y-2*r,x+r-2,y,ink);
    ht_triangle(ht_scene,x-r,y-2*r,x+r-2,y,x-r+2,y,ink);
    ht_limb(ht_scene,x-r,y-2*r,x+r,y-2*r,1,1,45);
    if(chipped)ht_triangle(ht_scene,x+r-3,y-2*r-1,x+r,y-2*r-1,x+r-1,y-2*r+3,22);
    ht_limb(ht_scene,x-r+2,y-r*2+3,x-r+3,y-3,0,0,ink-40);
}
static void ht_cut_lamp(int x,int y,int r,int ink) {
    ht_rect(ht_scene,x-r-2,y-3,2*r+4,4,ink);
    ht_triangle(ht_scene,x-r,y-3,x-r+2,y-r*3,x+r-2,y-r*3,ink);
    ht_triangle(ht_scene,x-r,y-3,x+r-2,y-r*3,x+r,y-3,ink);
    ht_rect(ht_scene,x-r+2,y-r*3+2,2*r-4,2*r,42);
    ht_rect(ht_scene,x-1,y-r*2,2,r+2,160);
    ht_limb(ht_scene,x-r+1,y-r*3,x-r+1,y-r*4,1,1,ink);
    ht_limb(ht_scene,x+r-1,y-r*3,x+r-1,y-r*4,1,1,ink);
    ht_limb(ht_scene,x-r+1,y-r*4,x+r-1,y-r*4,1,1,ink);
}
static void ht_cut_chair(int x,int y,bool tucked) {
    int dx=tucked?12:0;x+=dx;
    ht_limb(ht_scene,x-12,y-57,x-9,y-10,2,2,215);
    ht_limb(ht_scene,x+11,y-57,x+14,y-10,2,2,215);
    for(int n=0;n<3;++n)ht_limb(ht_scene,x-11,y-53+n*7,x+12,y-53+n*7,1,1,186);
    ht_limb(ht_scene,x-12,y-27,x+22,y-27,3,2,225);
    ht_limb(ht_scene,x+22,y-27,x+19,y,2,2,220);
    ht_limb(ht_scene,x-11,y-27,x-15,y,2,2,220);
}
static void ht_cutscene_draw_kitchen_forward(const ht_cutscene_state *s,const ht_cutscene_cue *cue) {
    ht_cutscene_camera camera=ht_cutscene_camera_at(s);
    bool night=cue->pose==HT_CUT_POSE_SIT,packing=cue->pose==HT_CUT_POSE_PACK;
    int flash=ht_cutscene_intro_flash(s),local=(int)s->tick-cue->start;
    ht_world_scale=256;ht_cutscene_fill(night?145:66);
    ht_world_scale=ht_cutscene_scale(s)*256/270;camera.framing=270;
    /* Shallow perspective room. Window is in the back wall, not floating above
     * a second unrelated white rectangle. Perspective seams meet that wall. */
    ht_rect(ht_scene,40,30,390,137,night?135:61);
    ht_triangle(ht_scene,0,10,40,30,40,167,night?190:104);
    ht_triangle(ht_scene,0,10,40,167,0,243,night?190:104);
    ht_triangle(ht_scene,430,30,480,10,480,242,night?175:93);
    ht_triangle(ht_scene,430,30,480,242,430,167,night?175:93);
    ht_rect(ht_scene,0,167,480,103,night?163:98);
    for(int i=0;i<10;++i) {
        int x=20+i*47;ht_limb(ht_scene,100+(x-100)*2/3,167,x,270,0,0,night?181:119);
    }
    for(int y=186;y<230;y+=20)ht_limb(ht_scene,20,y,460,y,0,0,night?176:107);
    /* The pane is almost empty country; fog swallows the last apple crown. */
    ht_rect(ht_scene,184,43,221,114,night?111:13);
    for(int y=44;y<156;++y)ht_rect(ht_scene,185,y,219,1,night?111:13+(y-44)/7);
    /* Farthest crown, wall, rake and bucket are separate depth/value planes. */
    for(int k=0;k<3;++k) {
        int x=329+k*22,y=126-k*7;
        ht_limb(ht_scene,x,145,x-5,y-42,2,1,night?136:51-k*8);
        ht_limb(ht_scene,x-3,y-18,x-31,y-40,1,0,night?128:45-k*7);
        ht_limb(ht_scene,x-2,y-21,x+22,y-35,1,0,night?128:45-k*7);
        for(int twig=0;twig<3;++twig)ht_limb(ht_scene,x-12-twig*7,y-26-twig*5,x-15-twig*8,y-43-twig*4,0,0,night?128:41-k*6);
    }
    ht_rect(ht_scene,190,133,207,21,night?149:89);
    for(int row=0;row<3;++row)for(int x=194+(row&1)*12;x<395;x+=28) {
        ht_limb(ht_scene,x,134+row*7,x+24,134+row*7,0,0,night?166:108);
        ht_limb(ht_scene,x,134+row*7,x,139+row*7,0,0,night?166:108);
    }
    ht_limb(ht_scene,221,148,231,111,1,1,night?183:148);
    ht_limb(ht_scene,222,112,239,117,1,1,night?183:148);
    for(int i=0;i<6;++i)ht_limb(ht_scene,222+i*3,113+i,220+i*3,119+i,0,0,160);
    ht_triangle(ht_scene,243,139,261,139,258,151,165);
    ht_triangle(ht_scene,243,139,258,151,246,151,165);
    ht_limb(ht_scene,246,138,249,131,0,0,154);ht_limb(ht_scene,249,131,257,133,0,0,154);
    /* A wiped crescent and sparse droplets keep the view tactile. */
    for(int n=0;n<12;++n) {
        int x=196+n*17,y=52+(n%4)*18;
        ht_limb(ht_scene,x,y,x-1,y+10+(n%3)*5,0,0,night?141:52);
    }
    ht_limb(ht_scene,283,71,267,79,1,0,night?154:59);
    ht_limb(ht_scene,267,79,260,91,1,0,night?154:59);
    ht_limb(ht_scene,260,91,268,110,1,0,night?154:59);
    /* Local flash only. No full-screen exposure pulse. */
    if(flash) {
        ht_disk(ht_scene,354,88,4,0);ht_limb(ht_scene,348,88,360,88,0,0,2);
        ht_limb(ht_scene,354,82,354,94,0,0,2);
    }
    ht_rect(ht_scene,177,35,235,8,195);ht_rect(ht_scene,177,43,7,118,202);
    ht_rect(ht_scene,405,43,7,118,207);ht_rect(ht_scene,292,43,5,112,173);
    ht_rect(ht_scene,180,98,225,4,173);
    ht_triangle(ht_scene,175,157,413,157,424,169,147);
    ht_triangle(ht_scene,175,157,424,169,166,169,147);
    ht_rect(ht_scene,166,169,258,5,223);
    /* Shelf, cups and absent handle-space. The chipped cup is the rightmost. */
    ht_rect(ht_scene,61,75,100,5,205);ht_limb(ht_scene,69,80,77,94,2,1,162);
    ht_limb(ht_scene,150,80,142,94,2,1,162);
    ht_cut_cup(76,74,6,false,172);ht_cut_cup(100,74,6,false,190);ht_cut_cup(129,74,7,true,210);
    /* Cast-iron stove and crossed slippers remain low in the departure view. */
    ht_rect(ht_scene,54,116,54,79,215);ht_rect(ht_scene,62,125,35,35,183);
    ht_rect(ht_scene,66,130,27,24,236);ht_disk(ht_scene,89,140,2,107);
    ht_rect(ht_scene,48,111,68,6,231);ht_rect(ht_scene,83,30,10,81,193);
    for(int y=43;y<105;y+=21)ht_limb(ht_scene,82,y,95,y,1,1,147);
    ht_limb(ht_scene,58,194,57,207,2,2,225);ht_limb(ht_scene,104,194,106,207,2,2,225);
    ht_limb(ht_scene,71,205,91,200,3,2,210);ht_limb(ht_scene,78,198,94,207,3,2,177);
    /* Counter: bevel, inset basin, cold water and hanging cloth. */
    ht_triangle(ht_scene,185,173,394,173,416,185,170);ht_triangle(ht_scene,185,173,416,185,164,185,170);
    ht_rect(ht_scene,164,185,252,30,209);ht_limb(ht_scene,166,185,414,185,1,1,237);
    ht_triangle(ht_scene,216,175,264,175,274,183,76);ht_triangle(ht_scene,216,175,274,183,206,183,76);
    ht_limb(ht_scene,222,177,258,178,0,0,8);
    ht_limb(ht_scene,253,174,253,153,2,2,196);ht_limb(ht_scene,253,153,244,151,2,2,196);
    ht_limb(ht_scene,244,152,241,158,1,1,196);
    ht_rect(ht_scene,375,182,19,37,119);ht_limb(ht_scene,379,185,380,216,0,0,148);
    ht_limb(ht_scene,387,186,388,216,0,0,146);
    /* Table and chair are human-sized and grounded in the same room. */
    ht_cut_chair(133,224,packing);
    ht_triangle(ht_scene,123,189,170,181,205,194,130);ht_triangle(ht_scene,123,189,205,194,155,202,130);
    ht_limb(ht_scene,129,192,131,229,2,2,194);ht_limb(ht_scene,194,196,191,226,2,2,194);
    ht_limb(ht_scene,155,201,155,240,2,2,210);
    if(night) {
        /* Seated dressed figure, knees folded, hands still. Window remains empty. */
        ht_disk(ht_scene,128,155,7,244);ht_limb(ht_scene,128,163,135,194,6,7,247);
        ht_limb(ht_scene,135,193,152,202,4,4,242);ht_limb(ht_scene,152,202,149,222,3,2,237);
        ht_limb(ht_scene,130,176,137,193,3,2,231);ht_limb(ht_scene,137,193,148,195,2,2,223);
        ht_limb(ht_scene,120,150,120,169,2,1,236);
    } else if(packing) {
        ht_cut_object actor={HT_CUT_OBJ_ACTOR,HT_CUT_POSE_PACK,HT_CUT_FACE_RIGHT_3Q,255,{130,-73,0},0,0,0,0};
        ht_cutscene_forward_actor(&camera,&actor);
        /* Bread, knife, rolled stockings, pantry lamp, notebook and torn accounts. */
        ht_disk(ht_scene,152,184,7,117);ht_rect(ht_scene,145,184,16,6,123);
        for(int i=0;i<3;++i)ht_limb(ht_scene,148+i*4,181,151+i*4,184,0,0,165);
        ht_limb(ht_scene,170,190,186,186,1,0,229);ht_rect(ht_scene,169,188,6,3,76);
        ht_cut_lamp(190,188,5,218);
        ht_rect(ht_scene,139,174,13,7,205);ht_rect(ht_scene,141,175,9,2,143);
        ht_triangle(ht_scene,159,179,176,175,180,183,234);ht_triangle(ht_scene,159,179,180,183,164,187,234);
        ht_limb(ht_scene,163,181,176,179,0,0,55);
        ht_triangle(ht_scene,154,206,166,210,147,213,18);
        ht_limb(ht_scene,151,210,160,210,0,0,112);
    } else {
        ht_cut_object actor={HT_CUT_OBJ_ACTOR,cue->pose,cue->face,255,{130,0,0},(int16_t)s->tick,0,0,0};
        ht_cutscene_forward_actor(&camera,&actor);
        /* Washing cup moves a few pixels with the hand, then stays un-rinsed. */
        int motion=cue->pose==HT_CUT_POSE_WASH?((local/18)%4-2):0;
        ht_cut_cup(264+motion,169,5,false,218);
        ht_limb(ht_scene,267,171,270,179,0,0,32);ht_disk(ht_scene,271,179,2,41);
    }
    if(night || packing) {ht_cut_cup(269,169,5,false,218);ht_disk(ht_scene,272,179,2,41);}
    /* Reflection is incomplete and stays inside the wiped pane. */
    if(!night && !packing) {
        ht_disk(ht_scene,275,118,6,63);ht_limb(ht_scene,275,126,271,150,4,3,59);
        ht_limb(ht_scene,271,135,284,145,1,1,55);
    }
    ht_world_scale=256;
}

/* Continuous triangular gust, interpolated through a 256-tick cycle. There is
 * no modulo jump in blade position, and each depth layer lags the same wind. */
static int ht_cut_grass_wind(unsigned tick,int lag) {
    unsigned phase=(tick+(unsigned)lag)&255u;
    return phase<128?(int)phase-64:192-(int)phase;
}
static void ht_cut_grass_blade(int x,int y,int height,int lean,int width,int tone,bool seed) {
    int px=x,py=y;
    for(int k=1;k<=6;++k) {
        int nx=x+lean*k*k/36,ny=y-height*k/6;
        int r=ht_max(0,width*(6-k)/6);
        ht_limb(ht_scene,px,py,nx,ny,ht_max(1,r),r,tone);px=nx;py=ny;
        if(k==2 || k==4) {
            int side=k==2?-1:1;
            ht_triangle(ht_scene,nx,ny+3,nx+side*(12+width*2),ny-12,nx+side*4,ny-4,tone);
        }
    }
    if(seed) for(int i=0;i<5;++i) {
        int sx=px+(i&1?2:-2),sy=py-i*3;
        ht_limb(ht_scene,px,sy+2,sx,sy-2,1,0,tone);
    }
}
static void ht_cutscene_draw_grass_forward(const ht_cutscene_state *s,const ht_cutscene_cue *cue) {
    bool warning=cue->caption==3;int flash=ht_cutscene_intro_flash(s);
    ht_world_scale=256;ht_cutscene_fill(19);
    /* Far house glimpsed through an opening only in the warning memory. */
    if(warning) {
        ht_rect(ht_scene,320,32,83,73,73);ht_triangle(ht_scene,307,34,360,8,414,34,99);
        ht_rect(ht_scene,350,44,23,32,202);ht_rect(ht_scene,348,41,27,4,135);
    }
    for(int layer=0;layer<3;++layer) {
        int count=22-layer*3;
        for(int i=0;i<count;++i) {
            int x=-8+i*26+(layer*13),y=191+layer*9;
            int h=103+(i*29+layer*37)%66,lean=ht_cut_grass_wind(s->tick,layer*29+i%3*5)/7;
            ht_cut_grass_blade(x,y,h,lean,2+layer,63+layer*40,(i%3)==0);
        }
        ht_checkpoint();
    }
    /* Body-sized flattened room: bent stems radiate from the two seated places. */
    ht_triangle(ht_scene,92,213,177,166,365,203,93);
    ht_triangle(ht_scene,92,213,365,203,342,231,93);
    for(int i=0;i<27;++i) {
        int x=100+(i*43)%254,y=196+(i*11)%31;
        ht_limb(ht_scene,x,y,x+16+(i%4)*4,y-4-(i%3),1,0,133+(i%3)*18);
    }
    /* Same scaled actor anatomy, close enough for hands and braid to read. */
    ht_cutscene_camera camera={HT_CUT_VIEW_FORWARD_X,0,0,48,310,420,143,6};
    ht_cut_object narrator={HT_CUT_OBJ_ACTOR,HT_CUT_POSE_CROUCH,HT_CUT_FACE_BACK,255,{82,-50,0},0,0,0,0};
    ht_cut_object sister={HT_CUT_OBJ_ACTOR,HT_CUT_POSE_CROUCH,warning?HT_CUT_FACE_RIGHT_3Q:HT_CUT_FACE_FRONT,255,{130,58,0},0,0,0,0};
    ht_cutscene_forward_actor(&camera,&sister);ht_cutscene_forward_actor(&camera,&narrator);
    /* Pocket mirror and reaching wrist, never a person-sized beacon. */
    ht_limb(ht_scene,283,180,267,177,2,1,227);ht_rect(ht_scene,264,173,5,7,113);
    if(flash) {ht_disk(ht_scene,266,174,3,0);ht_limb(ht_scene,259,174,273,174,0,0,5);}
    ht_limb(ht_scene,293,151,298,166,2,1,214); /* sister's loose braid */
    ht_disk(ht_scene,259,211,2,214);ht_limb(ht_scene,265,207,271,212,1,0,156); /* beetle/pod */
    /* Near leaves enclose rather than cover both faces. Shared gust has depth lag. */
    for(int i=0;i<14;++i) {
        int side=i<7?-1:1,j=i%7,x=side<0?22+j*15:365+j*14;
        ht_cut_grass_blade(x,254,147+(j*29)%71,side*(12+j*2)+ht_cut_grass_wind(s->tick,j*9)/5,4,222+(j%3)*10,true);
    }
    /* Entrance stems pulled across the low opening. */
    ht_limb(ht_scene,95,229,177,220,2,0,210);ht_limb(ht_scene,355,225,299,218,2,0,221);
}

/* A close insert before the exterior profile walk: coat, iron latch, taut
 * filament and a deliberate return of the hand. The wider doorway shot follows. */
static void ht_cutscene_draw_latch(const ht_cutscene_state *s,const ht_cutscene_cue *cue) {
    int local=(int)s->tick-cue->start;
    ht_world_scale=256;ht_cutscene_fill(65);
    ht_rect(ht_scene,18,22,172,210,124);ht_rect(ht_scene,186,20,18,210,229);
    ht_rect(ht_scene,204,20,252,210,15);
    /* Flat dawn rectangle still contains the wall and the last apple tree. */
    ht_rect(ht_scene,225,173,231,22,90);
    ht_organic_tree(ht_scene,395,204,171,9,7651,139,true);
    ht_rect(ht_scene,203,210,269,37,110);
    for(int x=208;x<450;x+=31)ht_limb(ht_scene,x,212,x+16,243,0,0,127);
    /* Ordinary domestic traces behind her, rather than an empty grey block. */
    ht_limb(ht_scene,65,150,164,150,4,3,170);ht_limb(ht_scene,71,153,72,213,3,2,173);
    ht_limb(ht_scene,154,153,153,212,3,2,173);
    ht_cut_chair(108,211,true);

    ht_rect(ht_scene,38,198,24,7,155);ht_limb(ht_scene,47,204,67,197,3,2,184);
    for(int y=46;y<196;y+=31)ht_limb(ht_scene,190,y,200,y+1,0,0,169);
    /* Side-on human insert shares the established articulated pose, with a
     * real hand target in the same projected coordinates as the iron latch. */
    int reach=ht_clamp(local<90?local:160-local,0,75);
    ht_game actor=ht;actor.facing=-1;actor.grounded=true;actor.vx=0;
    actor.traversal.mode=HT_FREE;actor.traversal.support=1;
    actor.traversal.forest_push_release=0;actor.landing_ticks=0;
    ht_person_pose pose=ht_person_pose_at(235,166,&actor);
    pose.hand[1]=ht_joint_at(234-reach*7/75,154-reach*7/75);
    ht_world_scale=640;ht_person_draw(&pose);ht_world_scale=256;
    ht_rect(ht_scene,236,177,13,18,214);ht_limb(ht_scene,228,157,240,177,1,1,148);
    ht_limb(ht_scene,198,161,208,161,2,1,205);ht_limb(ht_scene,208,161,208,168,1,1,205);
    if(local<102)ht_limb(ht_scene,207,166,228,187,0,0,180);
    if(local>=102 && local<126) {
        int hx=240+(pose.hand[1].x-240)*640/256,hy=135+(pose.hand[1].y-135)*640/256;
        ht_limb(ht_scene,hx,hy,hx+5,hy+7,0,0,141);
    }
}
