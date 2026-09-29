/* Narrative set dressing at world scale. These silhouettes sit behind the
 * player and interactive props; no screen-sized pass or live-state reads. */
static void ht_stitch(int x,int y,int wind) {
    int tip=x+10+wind/12;
    ht_limb(ht_scene,x,y,tip,y+4,2,1,150);
    ht_triangle(ht_scene,x,y+1,tip,y+4,tip-4,y+12,165);
    ht_limb(ht_scene,x+3,y+3,tip-2,y+7,0,0,35);
    for(int i=0;i<3;++i) ht_rect(ht_scene,x+3+i*2,y+2+i,1,3,255);
}
static void ht_tally(int x,int y,int count) {
    for(int i=0;i<count;++i) ht_rect(ht_scene,x+i*3,y,1,7,80);
    if(count>=5) ht_limb(ht_scene,x-1,y+6,x+13,y,0,0,80);
}
static void ht_signal(int x,int y,const ht_game *g) {
    ht_rect(ht_scene,x-2,y-63,4,63,220);
    ht_rect(ht_scene,x-6,y-65,12,21,255);
    ht_rect(ht_scene,x-4,y-63,8,17,95);
    /* Three flashes, then a long pause. The same circuit exists at the end. */
    unsigned phase=g->ticks%144u;bool lit=phase<54u && phase%18u<9u;
    if(g->level==9 && g->verdict==1) lit=false;
    ht_disk(ht_scene,x,y-57,3,lit?0:195);
    ht_rect(ht_scene,x-7,y-68,14,3,255);
    ht_limb(ht_scene,x,y-40,x+12,y-31,1,0,200);
}
static void ht_route_material(const ht_game *g,int l,int r,int top,int index) {
    int cx=g->camera/256,y=top-g->camera_y/256;
    if(y< -25 || y>HT_H+10) return;
    int begin=ht_max(l, cx-12),end=ht_min(r,cx+HT_W+12);
    /* Stable world-anchored grain, never regenerated from frame numbers. */
    for(int at=((begin+23)/24)*24;at<end;at+=24) {
        unsigned seed=ht_hash((unsigned)at+g->level*19u);
        int x=at-cx;
        if(g->level==1 || g->level==2 || g->level==3 || g->level==7 || g->level==9) {
            ht_rect(ht_scene,x,y+5,16,1,115);
            ht_rect(ht_scene,x+3,y+9,2,2,65);
            ht_rect(ht_scene,x+16,y+9,2,2,65);
            if(g->level==3) {ht_rect(ht_scene,x,y-3,18,2,210);ht_rect(ht_scene,x+3,y-1,3,2,255);}
        } else if(g->level==4) {
            ht_rect(ht_scene,x,y+1,2,13,80);
            ht_rect(ht_scene,x+4,y+5,13,1,155);
            if(index!=0 && index!=9) {
                ht_rect(ht_scene,x+9,y+18,3,25+(int)(seed%12),180);
                ht_limb(ht_scene,x+9,y+31,x+25,y+18,0,0,130);
            }
        } else {
            ht_limb(ht_scene,x,y+5,x+8,y+10,0,0,130);
            ht_limb(ht_scene,x+8,y+10,x+17,y+8,0,0,130);
            if(g->level==6) ht_rect(ht_scene,x,y+12,23,1,95);
            else ht_triangle(ht_scene,x+2,y+17,x+7,y+22+(int)(seed%6),x+13,y+17,220);
        }
    }
}
static void ht_story_landmarks(const ht_game *g) {
    int cx=g->camera/256,cy=g->camera_y/256,wind=ht_wind(g);
    /* Two authored scenes per chapter, keyed to its actual route supports. */
    for(int scene=0;scene<2;++scene) {
        int platform=scene?6:3;
        int world=ht_landmark_x(g->level,scene);
        int x=world-cx,y=ht_surface_at(g,platform,world)-cy;
        if(x< -110-ht_view_margin() || x>HT_W+110+ht_view_margin() || y< -25 || y>HT_H+120) continue;
        switch(g->level) {
        case 0: /* Cut trail post and cloth: two hands marked the same route. */
            ht_limb(ht_scene,x,y,x-3,y-51,4,3,220);
            ht_limb(ht_scene,x-3,y-45,x+20,y-53,2,0,220);
            ht_stitch(x-2,y-44,wind);
            ht_limb(ht_scene,x-5,y-25,x+4,y-31,0,0,45);
            ht_limb(ht_scene,x-5,y-20,x+4,y-26,0,0,45);
            ht_rect(ht_scene,x+17,y-15,18,15,175);
            ht_rect(ht_scene,x+20,y-13,12,10,55);
            ht_tally(x+21,y-12,scene?3:2);
            break;
        case 1: /* Locked service door beside a deliberately lowered route. */
            ht_rect(ht_scene,x-20,y-68,42,68,135);
            ht_rect(ht_scene,x-17,y-65,34,63,60);
            ht_rect(ht_scene,x-14,y-62,28,58,150);
            ht_rect(ht_scene,x-12,y-58,24,14,85);
            for(int k=0;k<3;++k) ht_rect(ht_scene,x-10+k*8,y-56,2,10,170);
            ht_rect(ht_scene,x+8,y-29,4,8,255);
            ht_disk(ht_scene,x+10,y-29,3,255);ht_disk(ht_scene,x+10,y-29,1,55);
            ht_limb(ht_scene,x-42,y-38,x-25,y-38,1,1,175);
            ht_triangle(ht_scene,x-25,y-42,x-25,y-34,x-19,y-38,175);
            ht_limb(ht_scene,x-39,y-44,x-28,y-32,0,0,255);
            ht_signal(x+41,y,g);
            break;
        case 2: /* Still machinery, measured fuel and a live service lamp. */
            ht_rect(ht_scene,x-29,y-27,26,27,220);ht_rect(ht_scene,x-26,y-24,20,21,110);
            ht_rect(ht_scene,x-22,y-32,13,5,255);ht_rect(ht_scene,x-19,y-30,7,3,0);
            ht_tally(x-24,y-19,7);
            ht_limb(ht_scene,x-22,y-8,x-10,y-8,0,0,40);
            ht_rect(ht_scene,x+8,y-37,31,37,185);
            ht_disk(ht_scene,x+23,y-27,9,255);ht_disk(ht_scene,x+23,y-27,6,35);
            ht_limb(ht_scene,x+23,y-27,x+26,y-31,0,0,230);
            ht_rect(ht_scene,x+13,y-12,21,3,90);
            ht_limb(ht_scene,x+38,y-9,x+51,y-9,2,2,200);
            ht_signal(x+58,y,g);
            break;
        case 3: /* Bench, cancelled ticket and a railway signal. */
            ht_rect(ht_scene,x-33,y-19,55,4,200);ht_rect(ht_scene,x-30,y-35,50,12,155);
            ht_rect(ht_scene,x-27,y-16,3,16,210);ht_rect(ht_scene,x+15,y-16,3,16,210);
            ht_rect(ht_scene,x-9,y-34,11,14,25);ht_rect(ht_scene,x-7,y-31,7,1,170);
            ht_limb(ht_scene,x-8,y-23,x,y-30,0,0,230);
            ht_signal(x+40,y,g);
            ht_limb(ht_scene,x+39,y-40,x+61,y-46,2,1,185);
            break;
        case 4: /* Matching records on opposite banks, abandoned ferry bell. */
            ht_rect(ht_scene,x-25,y-50,3,50,200);ht_rect(ht_scene,x+20,y-50,3,50,200);
            ht_rect(ht_scene,x-28,y-52,54,4,210);
            ht_rect(ht_scene,x-18,y-42,30,25,170);ht_rect(ht_scene,x-16,y-40,26,21,35);
            ht_tally(x-12,y-36,5);ht_tally(x-12,y-26,scene?4:5);
            ht_disk(ht_scene,x+36,y-20,7,190);ht_rect(ht_scene,x+27,y-18,18,3,230);
            ht_limb(ht_scene,x+36,y-27,x+36,y-45,0,0,180);
            for(int k=0;k<5;++k) ht_limb(ht_scene,x-43+k*4,y,x-48+k*5,y-17-k%3*5,0,0,200);
            break;
        case 5: /* Repaired hoist: a stitched grip and seven chalk marks. */
            ht_rect(ht_scene,x-21,y-60,5,60,185);ht_rect(ht_scene,x+26,y-60,5,60,185);
            ht_rect(ht_scene,x-24,y-64,58,5,215);
            ht_disk(ht_scene,x+5,y-55,8,225);ht_disk(ht_scene,x+5,y-55,5,70);
            ht_limb(ht_scene,x+5,y-55,x+5,y-26,0,0,220);
            ht_rect(ht_scene,x-4,y-25,18,4,200);ht_stitch(x+1,y-25,wind);
            ht_tally(x-13,y-12,7);
            for(int k=0;k<6;++k) ht_rect(ht_scene,x-50+k*6,y-1,3,1,85);
            break;
        case 6: /* Broken glazing, tended seedlings and an inside door latch. */
            ht_limb(ht_scene,x-42,y,x-42,y-61,1,1,155);
            ht_limb(ht_scene,x+42,y,x+42,y-61,1,1,155);
            ht_limb(ht_scene,x-42,y-61,x,y-82,1,1,155);
            ht_limb(ht_scene,x,y-82,x+42,y-61,1,1,155);
            ht_limb(ht_scene,x,y-82,x,y,1,1,155);
            ht_limb(ht_scene,x-42,y-40,x+42,y-40,0,0,120);
            ht_triangle(ht_scene,x+3,y-76,x+30,y-61,x+7,y-57,45);
            ht_limb(ht_scene,x+3,y-76,x+7,y-57,0,0,145);
            ht_rect(ht_scene,x+26,y-27,14,3,230);
            for(int k=0;k<5;++k) {
                int sx=x-31+k*13;ht_rect(ht_scene,sx-4,y-5,9,5,195);
                ht_limb(ht_scene,sx,y-5,sx,y-17,0,0,210);
                ht_limb(ht_scene,sx,y-11,sx-5,y-15,1,0,155);
                ht_limb(ht_scene,sx,y-14,sx+4,y-19,1,0,155);
            }
            break;
        case 7: /* A return pipe and two feeds from one valve. */
            ht_limb(ht_scene,x-48,y-12,x+45,y-12,4,4,180);
            ht_limb(ht_scene,x-10,y-12,x-10,y-45,3,3,195);
            ht_limb(ht_scene,x-10,y-45,x+32,y-45,3,3,195);
            ht_disk(ht_scene,x-10,y-29,12,225);ht_disk(ht_scene,x-10,y-29,9,60);
            for(int k=-1;k<=1;k+=2) ht_limb(ht_scene,x-10+k*7,y-36,x-10-k*7,y-22,1,1,210);
            ht_disk(ht_scene,x-10,y-29,3,240);
            for(int k=0;k<2;++k) {ht_rect(ht_scene,x+29,y-56+k*18,16,11,165);ht_rect(ht_scene,x+32,y-53+k*18,9,4,40);}
            ht_stitch(x-14,y-36,wind);
            break;
        case 8: /* Guyed signal tower: the familiar stitch predates the route. */
            ht_limb(ht_scene,x-14,y,x,y-99,2,1,215);ht_limb(ht_scene,x+14,y,x,y-99,2,1,215);
            for(int k=0;k<4;++k) ht_limb(ht_scene,x-12+k*2,y-k*19,x+10-k*2,y-19-k*19,0,0,165);
            ht_limb(ht_scene,x,y-81,x-45,y,0,0,105);ht_limb(ht_scene,x,y-81,x+45,y,0,0,105);
            ht_stitch(x+1,y-48,wind);ht_signal(x+1,y-33,g);
            break;
        case 9: /* A writing arm linked to the same three-flash signal. */
            ht_rect(ht_scene,x-23,y-39,43,39,160);ht_rect(ht_scene,x-20,y-36,37,30,45);
            ht_rect(ht_scene,x-16,y-14,28,8,15);
            for(int k=0;k<3;++k) ht_rect(ht_scene,x-12,y-12+k*2,15-k*3,1,115);
            ht_disk(ht_scene,x-12,y-28,4,210);
            int pen=(g->verdict!=1)?(int)((g->ticks/4)%12):0;
            ht_limb(ht_scene,x-12,y-28,x+5+pen/3,y-19,1,1,220);
            ht_limb(ht_scene,x+5+pen/3,y-19,x+pen/2,y-10,0,0,220);
            ht_signal(x+38,y,g);
            ht_limb(ht_scene,x+19,y-25,x+38,y-25,0,0,160);
            break;
        }
    }
}

/* Close reading of physical details adds an optional second layer without
 * opening the journal or consuming a prop/evidence interaction. */
static const char *const ht_observations[20][2]={
    {"THE CLOTH IS NEW. THE CUT BENEATH IT IS OLD.","SOMEONE REUSED A ROUTE THAT WAS ALREADY MARKED."},
    {"TWO NOTCHES FACE BACK. THREE FACE FORWARD.","NEITHER HAND REMOVED THE OTHER HANDS MARK."},
    {"THE PADLOCK IS SHUT. THE LADDER IS LOWERED.","SOMEONE LEFT ONE WAY THROUGH."},
    {"THREE FLASHES. A PAUSE. THREE AGAIN.","THE WINDOW BEHIND THE LAMP IS EMPTY."},
    {"SEVEN CUTS IN THE CAN. ONE LINE DIVIDES THEM.","THE METAL IS WORN WHERE THE LINE WAS TOUCHED."},
    {"THE PRESSURE NEEDLE RESTS AT ZERO.","THE SERVICE LAMP STILL RECEIVES POWER."},
    {"THE TICKET HAS BEEN CANCELLED BY HAND.","THE BENCH HAS ROOM FOR SOMEONE TO WAIT."},
    {"THE SIGNAL MOVES. THE TRACKS ARE SILENT.","A WORKING SIGNAL DOES NOT PROVE A TRAIN CAME."},
    {"FIVE MARKS ARE CUT INTO THIS BANKS BOARD.","THE BELL ROPE HAS BEEN LEFT WITHIN REACH."},
    {"THE SECOND BOARD HAS ONE FEWER MARK.","THE OLD STROKE IS STILL VISIBLE BENEATH IT."},
    {"HER STITCHES COVER THE SPLIT IN THE GRIP.","THE CLOTH WAS USED BEFORE IT BECAME A SIGN."},
    {"SEVEN MARKS. THE LAST IS CUT MORE DEEPLY.","SMALL PRINTS KEEP TO THE EDGE OF THE HOIST."},
    {"THE GLASS IS BROKEN. THE ROWS ARE TENDED.","SOMEONE RETURNED AFTER THE ROOF GAVE WAY."},
    {"THE LATCH CAN BE REACHED FROM INSIDE.","THAT EXPLAINS THE LOCK. NOT WHO CLOSED IT."},
    {"ONE VALVE. TWO FEEDS.","THE RETURN PIPE IS NOT AN ABANDONED LINE."},
    {"THE SAME STITCH IS TIED BESIDE THE WHEEL.","A WARNING AND A HANDHOLD CAN LOOK ALIKE."},
    {"HER MARK IS BELOW THE OLDER GUY WIRE.","IT WAS HERE BEFORE THIS LAST REPAIR."},
    {"THE RIDGE ANSWERS IN THREE FLASHES.","I CANNOT SEE A PERSON AT THE SIGNAL."},
    {"THE PEN ARM MOVES WITH THE LAMP CIRCUIT.","A MOVING PEN DOES NOT TELL ME WHO WROTE."},
    {"THE RETURN WIRE DISAPPEARS INTO THE WALL.","THE LIGHT AND THE WORDS SHARE A SOURCE."}
};
static void ht_observation_prompt(const ht_game *g) {
    if(g->observation_ticks && g->observation && g->observation<=20) {
        ht_rect(ht_scene,66,33,348,29,255);
        for(int i=0;i<2;++i) {
            const char *text=ht_observations[g->observation-1][i];
            if(g->level==9 && g->verdict==1)
                text=i?"A SILENT MACHINE DOES NOT ERASE ITS WORDS.":"THE ARM HAS STOPPED. THE PAPER REMAINS.";
            ht_text_color(74,38+i*11,text,1,0);
        }
    } else if(ht_observe_near(g)>=0) {
        ht_rect(ht_scene,66,33,348,19,255);
        ht_text_color(74,38,"A / CONFIRM - EXAMINE WHAT WAS LEFT HERE",1,0);
    }
}
static void ht_contact_detail(const ht_game *g) {
    if(g->landing_ticks) {
        int age=10-g->landing_ticks,x=g->landing_x-g->camera/256,y=g->landing_y-g->camera_y/256;
        for(int i=0;i<g->landing_strength;++i) {
            int side=i%2?1:-1,spread=2+age+i*2;
            ht_rect(ht_scene,x+side*spread,y-1-(age*(10-age))/15-i%2,2,1,120+age*10);
        }
    }
    const ht_mechanics *m=ht_mech(g);
    if(m->crate_x && ht_abs(g->traversal.crate_vx)>32 && !g->traversal.crate_vy) {
        int x=g->traversal.crate_x/256-g->camera/256,y=g->traversal.crate_y/256-g->camera_y/256;
        int side=g->traversal.crate_vx>0?-1:1;
        for(int i=0;i<3;++i) ht_rect(ht_scene,x+side*(14+i*4),y-1-(int)((g->ticks+i)%3),2,1,140+i*30);
    }
}
