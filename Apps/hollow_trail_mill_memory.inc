/* Novella I, the mill register: a memory finding furniture to inhabit.
 * The close inserts share the normal articulated limbs and raster primitives.
 * No texture, random frame noise, extra arena or gameplay-state mutation. */
static int ht_register_ease(unsigned tick,unsigned start,unsigned end) {
    int u=ht_clamp(((int)tick-(int)start)*256/(int)(end-start),0,256);
    return u*u*(768-2*u)/65536;
}
static void ht_register_quad(int ax,int ay,int bx,int by,int cx,int cy,int dx,int dy,int ink) {
    ht_triangle(ht_scene,ax,ay,bx,by,cx,cy,ink);
    ht_triangle(ht_scene,ax,ay,cx,cy,dx,dy,ink);
}
static void ht_register_loaf(int x,int y,int ink) {
    ht_limb(ht_scene,x-7,y,x+7,y,6,6,ink);
    ht_disk(ht_scene,x-7,y,6,ink);ht_disk(ht_scene,x+7,y,6,ink);
    ht_limb(ht_scene,x-10,y+4,x+10,y+4,1,1,ink);
    for(int i=0;i<3;++i)ht_limb(ht_scene,x-6+i*5,y-3,x-3+i*5,y,0,0,ink+40);
}
/* The existing silhouette's proportions and two-bone contacts, with unequal
 * shoulder roots and loose hair supplied by this specific remembered action. */
static ht_joint ht_register_project(ht_joint p,int scale) {
    return ht_joint_at(240+(p.x-240)*scale/256,135+(p.y-135)*scale/256);
}
static void ht_register_arm(ht_joint root,ht_joint hand,int bend,int scale,int ink) {
    ht_joint elbow=ht_person_joint(root,hand,6*scale/256,6*scale/256,bend);
    ht_limb(ht_scene,root.x,root.y,elbow.x,elbow.y,6,5,ink);
    ht_disk(ht_scene,elbow.x,elbow.y,5,ink);
    ht_limb(ht_scene,elbow.x,elbow.y,hand.x,hand.y,5,3,ink);
    ht_disk(ht_scene,hand.x,hand.y,4,ink);
}
static void ht_register_sister(const ht_person_pose *p,int hair,int scale) {
    ht_world_scale=256;
    ht_joint back=ht_register_project(ht_joint_at(p->shoulder.x-1,p->shoulder.y+2),scale);
    ht_joint front=ht_register_project(ht_joint_at(p->shoulder.x+1,p->shoulder.y-1),scale);
    ht_joint hip=ht_register_project(p->hip,scale);
    for(int i=0;i<2;++i) {
        ht_joint foot=ht_register_project(p->foot[i],scale);
        ht_joint knee=ht_person_joint(hip,foot,8*scale/256,7*scale/256,p->knee[i]);
        int ink=i?250:226;
        ht_limb(ht_scene,hip.x,hip.y,knee.x,knee.y,8,6,ink);
        ht_disk(ht_scene,knee.x,knee.y,6,ink);
        ht_limb(ht_scene,knee.x,knee.y,foot.x,foot.y-3,6,4,ink);
        ht_limb(ht_scene,foot.x-3,foot.y-2,foot.x+9,foot.y-2,3,2,ink);
    }
    ht_register_arm(back,ht_register_project(p->hand[0],scale),p->elbow[0],scale,229);
    /* Rounded shoulders and a slightly uneven cloth hem, not an enlarged
     * four-pixel jacket. Joint construction still is the shared character's. */
    ht_register_quad(back.x-7,back.y-2,front.x+7,front.y-2,hip.x+16,hip.y+12,hip.x-16,hip.y+10,248);
    ht_disk(ht_scene,back.x,back.y,7,248);ht_disk(ht_scene,front.x,front.y,7,248);
    ht_joint neck=ht_register_project(ht_joint_at(p->shoulder.x+1,p->shoulder.y-3),scale);
    ht_limb(ht_scene,neck.x,neck.y,front.x-4,front.y,5,5,248);
    ht_joint head=ht_register_project(ht_joint_at(p->shoulder.x+1,p->shoulder.y-6),scale);
    int hx=head.x,hy=head.y;
    ht_disk(ht_scene,hx-2,hy-1,13,247);
    ht_disk(ht_scene,hx+2,hy+6,8,247);
    ht_triangle(ht_scene,hx+9,hy-2,hx+14,hy+3,hx+9,hy+6,247);
    int tipx=hx+11-hair*19/256,tipy=hy+44-hair*18/256;
    ht_disk(ht_scene,hx-7,hy-4,10,237);
    ht_limb(ht_scene,hx-11,hy-4,hx-14,hy+19,6,4,237);
    ht_disk(ht_scene,hx-14,hy+19,4,237);
    ht_limb(ht_scene,hx+3,hy-11,hx+11-hair*15/256,hy+11,6,5,242);
    ht_limb(ht_scene,hx+11-hair*15/256,hy+11,tipx,tipy,5,2,242);
    ht_disk(ht_scene,tipx,tipy,2,242);
    ht_register_arm(front,ht_register_project(p->hand[1],scale),p->elbow[1],scale,255);
    ht_limb(ht_scene,back.x,back.y+6,hip.x-7,hip.y+5,0,0,205);
}
static ht_person_pose ht_register_pose(int x,int feet,int facing) {
    ht_game actor=ht_cutscene_anchor;
    actor.facing=facing;actor.grounded=true;actor.vx=actor.vy=0;
    memset(&actor.traversal,0,sizeof(actor.traversal));actor.traversal.support=1;
    actor.landing_ticks=0;
    return ht_person_pose_at(x,feet,&actor);
}
static void ht_register_room(void) {
    ht_cutscene_fill(64);
    ht_rect(ht_scene,34,24,410,149,79);
    ht_register_quad(0,4,34,24,34,173,0,221,119);
    ht_register_quad(444,24,480,4,480,224,444,173,108);
    ht_rect(ht_scene,0,173,480,97,124);
    for(int i=0;i<11;++i) {
        int x=8+i*46;ht_limb(ht_scene,125+(x-125)*3/4,173,x,270,0,0,148);
    }
    for(int y=197;y<232;y+=23)ht_limb(ht_scene,25,y,455,y,0,0,143);
    /* Broken window grid and one held shaft across the dust. */
    ht_rect(ht_scene,291,40,123,100,18);
    ht_register_quad(291,139,413,139,382,224,155,224,47);
    ht_limb(ht_scene,328,138,257,224,2,2,111);
    ht_limb(ht_scene,373,138,337,224,2,2,105);
    ht_limb(ht_scene,258,187,398,187,2,2,95);
    ht_organic_tree(ht_scene,393,135,82,4,5671,51,true);
    ht_rect(ht_scene,285,34,135,6,204);ht_rect(ht_scene,285,40,6,105,211);
    ht_rect(ht_scene,414,40,6,105,220);ht_rect(ht_scene,288,140,132,7,222);
    ht_rect(ht_scene,329,40,4,102,191);ht_rect(ht_scene,372,40,4,102,188);
    ht_rect(ht_scene,291,83,123,4,197);
    ht_triangle(ht_scene,333,42,357,42,333,65,59);
    ht_triangle(ht_scene,374,119,405,139,375,139,67);
    /* A shut leaf remains distinct from the broken window. */
    ht_rect(ht_scene,68,52,73,121,113);
    for(int x=73;x<140;x+=14)ht_limb(ht_scene,x,55,x,170,0,0,144);
    ht_limb(ht_scene,68,94,141,114,3,3,191);
}
static void ht_register_draw_desk_memory(unsigned tick) {
    ht_register_room();
    /* A sloping board with its hand-worn edge, paper and solid grounded legs. */
    ht_limb(ht_scene,235,168,236,218,3,2,208);
    ht_limb(ht_scene,341,157,337,216,3,2,193);
    ht_limb(ht_scene,249,174,254,221,3,2,229);
    ht_limb(ht_scene,236,202,338,200,2,2,184);
    ht_register_quad(225,164,327,143,350,155,248,177,164);
    ht_limb(ht_scene,225,165,248,178,3,2,221);
    ht_limb(ht_scene,248,178,350,156,2,2,227);
    ht_limb(ht_scene,234,168,242,172,1,1,106);
    ht_register_quad(251,160,304,149,327,155,274,168,31);
    for(int i=0;i<5;++i)ht_limb(ht_scene,258+i*3,161+i,308+i*2,152+i,0,0,110);
    /* Her raised shoulder, low pen and the back of the other wrist. */
    int reach=ht_register_ease(tick,292,344)-ht_register_ease(tick,390,455);
    int tucked=ht_register_ease(tick,326,373);
    ht_person_pose p=ht_register_pose(229,151,1);
    p.hip=ht_joint_at(229,137);p.shoulder=ht_joint_at(234,133);
    p.foot[0]=ht_joint_at(227,151);p.foot[1]=ht_joint_at(232,151);
    p.hand[0]=ht_joint_at(241,140);
    p.hand[1]=ht_joint_at(242-reach*5/256,140-reach*12/256);
    p.elbow[0]=-1;p.elbow[1]=1;
    ht_register_sister(&p,tucked,1280);
    /* Ink-stained fingers hold the nib close to the paper, not at its top. */
    ht_world_scale=1280;
    ht_limb(ht_scene,p.hand[0].x,p.hand[0].y-3,p.hand[0].x+2,p.hand[0].y+2,0,0,244);
    ht_disk(ht_scene,p.hand[0].x,p.hand[0].y,1,255);
    ht_world_scale=256;
}
static void ht_register_draw_departure(unsigned tick) {
    ht_cutscene_fill(101);
    ht_rect(ht_scene,34,24,160,192,118);
    ht_rect(ht_scene,199,29,228,187,17);
    ht_rect(ht_scene,193,21,9,201,228);ht_rect(ht_scene,426,21,10,201,216);
    ht_rect(ht_scene,194,21,240,8,196);
    /* Empty country beyond the doorway; the same ordinary domestic room. */
    ht_rect(ht_scene,202,165,224,34,64);
    ht_limb(ht_scene,204,170,423,176,2,2,99);
    ht_organic_tree(ht_scene,404,180,143,6,7651,93,true);
    ht_register_quad(200,212,427,212,480,251,144,251,129);
    for(int i=0;i<6;++i)ht_limb(ht_scene,210+i*40,214,185+i*53,250,0,0,152);
    ht_rect(ht_scene,76,62,95,5,189);ht_cut_cup(104,61,6,false,203);
    ht_cut_cup(140,61,7,true,209);
    ht_cut_chair(101,215,true);
    ht_limb(ht_scene,46,159,163,150,3,2,189);
    ht_limb(ht_scene,52,160,54,215,2,2,191);
    /* She makes space for a second loaf, then tries the weighted strap.
     * The first loaf remains in the bag throughout; no invented gesture reply. */
    int place=ht_register_ease(tick,535,660);
    int shoulder=ht_register_ease(tick,724,795);
    int bx=290,by=164-shoulder*7/256;
    ht_limb(ht_scene,259,112,bx-6,by-12,2,2,186);
    int loafx=ht_cutscene_lerp(310,bx-12,place),loafy=ht_cutscene_lerp(140,by-7,place);
    ht_person_pose p=ht_register_pose(241,151,1);
    p.shoulder=ht_joint_at(243,130);p.hip=ht_joint_at(240,137);
    p.foot[0]=ht_joint_at(237,151);p.foot[1]=ht_joint_at(244,151);
    p.hand[0]=ht_joint_at(247,139-shoulder*9/256);
    p.hand[1]=ht_joint_at(254-place*5/256,136+place*3/256-shoulder*2/256);
    p.elbow[0]=1;p.elbow[1]=-1;
    ht_register_sister(&p,256,1280);
    ht_register_loaf(bx+10,by-13,123);
    ht_register_loaf(loafx,loafy-3,152);
    ht_register_quad(bx-24,by-10,bx+25,by-10,bx+20,by+29,bx-20,by+29,210);
    ht_limb(ht_scene,bx-24,by-10,bx+25,by-10,2,2,240);
    ht_limb(ht_scene,bx-15,by-5,bx-12,by+23,0,0,167);
    ht_limb(ht_scene,bx+10,by-5,bx+8,by+25,0,0,175);
    /* The scarf is half wound: one loose end extends below the coat hem. */
    ht_world_scale=1280;
    ht_limb(ht_scene,241,127,247,129,1,1,167);
    ht_limb(ht_scene,246,129,247,145,1,1,179);
    ht_limb(ht_scene,247,145,249,149,1,0,169);
    ht_world_scale=256;
}
/* Seven unnamed rows retain both handwriting rhythms. Names are not supplied
 * by the novella, so their ink is a drawing, never invented readable names. */
static void ht_register_names(int dx,int dy) {
    for(int row=0;row<7;++row) {
        int y=65+row*13+dy;
        ht_limb(ht_scene,131+dx,y+7,327+dx,y+7,0,0,72);
        for(int n=0;n<12-(row%3);++n) {
            int x=147+n*8+dx,h=3+(n+row)%4;
            ht_limb(ht_scene,x,y+3,x+2,y-h,0,0,176);
            ht_limb(ht_scene,x+2,y-h,x+3,y+3,0,0,186);
            ht_limb(ht_scene,x+3,y+3,x+7,y+1-(n%2),0,0,176);
        }
        ht_rect(ht_scene,306+dx,y-4,9,9,183);
        ht_rect(ht_scene,307+dx,y-3,7,7,row==0 || row==2?157:24);
        if(row==3 || row==5)for(int n=0;n<4;++n)
            ht_limb(ht_scene,304+n*3+dx,y+5,309+n*3+dx,y-5,0,0,109);
    }
}
static void ht_register_draw_page(unsigned tick) {
    bool returned=tick>=900,keeper=tick>=1280;
    int tremble=returned && tick<970?ht_sway_wave((tick-900)*48)/170:0;
    int cover=returned?ht_register_ease(tick,970,1030)-ht_register_ease(tick,1110,1190):0;
    ht_cutscene_fill(100);
    /* Desk edge and leather binding fix the page in the same sloping plane. */
    ht_register_quad(49,42,397,17,444,212,65,236,165);
    ht_register_quad(88+tremble,43,350+tremble,28,393+tremble,218,109+tremble,222,224);
    ht_register_quad(94+tremble,43,345+tremble,33,385+tremble,215,114+tremble,216,23);
    for(int i=0;i<4;++i)ht_limb(ht_scene,99+i+tremble,47,118+i+tremble,211,0,0,64+i*12);
    /* Scraped-out heading, ruled names and the damaged word admission. */
    for(int i=0;i<7;++i)ht_limb(ht_scene,148+i*2+tremble,46+i,256+i*4+tremble,44+i,0,0,91+(i&1)*26);
    ht_register_names(tremble,0);
    ht_text_color(133+tremble,165,"RECORD EVERY ADMISSION",1,173);
    ht_limb(ht_scene,210+tremble,165,256+tremble,172,1,0,203);
    ht_limb(ht_scene,209+tremble,170,258+tremble,166,1,0,217);
    ht_text_color(139+tremble,180,"I WROTE FOR THEM.",1,211);
    /* Thumb grips the right edge. Only the paper trembles against it. */
    ht_register_quad(411,214,464,229,461,267,403,261,234);
    ht_limb(ht_scene,425,231,394,185,11,8,244);
    ht_limb(ht_scene,398,192,374,165,6,5,247);
    ht_limb(ht_scene,377,168,374,158,4,4,247);
    ht_limb(ht_scene,374,159,378,167,1,1,192);
    if(returned) {
        /* Left hand crosses only the six lower rows; her row stays visible. */
        /* The whole hand rises onto the first lower name, not just its rule.
         * Its uppermost fingertip stops below her own ink (last stroke y68). */
        int ox=-(256-cover)*300/256,oy=(256-cover)*140/256-cover*7/256;
        ht_limb(ht_scene,105+ox,252+oy,175+ox,151+oy,21,23,245);
        ht_disk(ht_scene,175+ox,151+oy,20,250);
        ht_register_quad(145+ox,158+oy,144+ox,106+oy,211+ox,104+oy,228+ox,149+oy,250);
        for(int i=0;i<4;++i) {
            int x=151+i*17+ox,top=83+(i==0?6:i==3?4:0)+oy;
            ht_limb(ht_scene,x,124+oy,x+4,top,8,6,250);
            ht_disk(ht_scene,x+4,top,6,250);
            ht_limb(ht_scene,x-2,101+oy,x+4,101+oy,0,0,207);
        }
        ht_limb(ht_scene,208+ox,138+oy,245+ox,115+oy,10,7,250);
        ht_disk(ht_scene,245+ox,115+oy,7,250);
        ht_limb(ht_scene,226+ox,124+oy,229+ox,128+oy,0,0,207);
    }
    if(keeper) {
        /* The last written line is finally read below the uncovered column. */
        ht_rect(ht_scene,130,192,238,14,23);
        ht_text_color(133,194,"I KEPT THE LAST SQUARE OPEN.",1,190);
        ht_text_color(285,204,"THE KEEPER",1,170);
    }
}
static void ht_cutscene_draw_register_memory(const ht_cutscene_state *s) {
    ht_world_scale=256;
    if(s->tick>=180 && s->tick<500)ht_register_draw_desk_memory(s->tick);
    else if(s->tick>=500 && s->tick<900)ht_register_draw_departure(s->tick);
    else ht_register_draw_page(s->tick);
    ht_world_scale=256;
}
