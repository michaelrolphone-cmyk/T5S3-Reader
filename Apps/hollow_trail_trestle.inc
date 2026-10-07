/* One reversible traversal of the actual supported members. Phase is bounded,
 * neutral input never moves it, and the missing span remains a rope crossing. */
static int ht_trestle_near(const ht_game *g) {
    if(g->level!=3 || !g->grounded || g->traversal.mode)return 0;
    int x=g->x/256,y=g->y/256;
    if(y==220 && x>=1232 && x<=1236 && g->facing>0)return 1;
    if(y==220 && x>=1488 && x<=1492 && g->facing<0)return -1;
    if(y==HT_TRESTLE_FLOOR && x>=1245 && x<=1475)return 2;
    return 0;
}
static void ht_trestle_enter(void) {
    ht_traversal *t=&ht.traversal;int side=ht_trestle_near(&ht),x=ht.x/256;
    t->trestle_tested=false;
    if(side==1)t->trestle_phase=0;
    else if(side==-1){t->trestle_phase=HT_TRESTLE_STEPS;t->trestle_tested=true;}
    else if(x<=1360)t->trestle_phase=(uint16_t)(80+ht_clamp(x-1246,0,114));
    else{t->trestle_phase=(uint16_t)(x<1370?242+(x-1360)*16/10:258+ht_clamp(x-1370,0,104));t->trestle_tested=true;}
    ht.coyote=ht.jump_buffer=0;
}
static void ht_trestle_step(int direction) {
    ht_traversal *t=&ht.traversal;int p=t->trestle_phase;
    if(p==HT_TRESTLE_TOUCH && direction>0 && !t->trestle_tested)direction=0;
    p=ht_clamp(p+direction,0,HT_TRESTLE_STEPS);t->trestle_phase=(uint16_t)p;
    int x,y=220;
    if(p<16)x=ht_shutter_lerp(1234,1246,p,0,16);
    else if(p<80){x=1246;y=ht_shutter_lerp(220,HT_TRESTLE_FLOOR,p,16,80);}
    else if(p<HT_TRESTLE_TOUCH){x=1246+p-80;y=HT_TRESTLE_FLOOR;}
    else if(p<HT_TRESTLE_TEST_END){x=ht_shutter_lerp(1360,1370,p,HT_TRESTLE_TOUCH+48,HT_TRESTLE_TEST_END);y=HT_TRESTLE_FLOOR;}
    else if(p<362){x=1370+p-HT_TRESTLE_TEST_END;y=HT_TRESTLE_FLOOR;}
    else if(p<426){x=1474;y=ht_shutter_lerp(HT_TRESTLE_FLOOR,220,p,362,426);}
    else x=ht_shutter_lerp(1474,1490,p,426,HT_TRESTLE_STEPS);
    ht.x=x*256;ht.y=y*256;ht.vx=ht.vy=0;ht.grounded=y==HT_TRESTLE_FLOOR || (!p || p==HT_TRESTLE_STEPS);
    ht.coyote=ht.jump_buffer=0;
    if(p>=80 && p<=362 && direction) {
        int print=x+8;
        if(!t->trestle_print_left)t->trestle_print_left=t->trestle_print_right=(int16_t)print;
        t->trestle_print_left=(int16_t)ht_min(t->trestle_print_left,print);
        t->trestle_print_right=(int16_t)ht_max(t->trestle_print_right,print);
    }
    if((!p && direction<0) || (p==HT_TRESTLE_STEPS && direction>0)) {
        t->mode=HT_FREE;ht.grounded=true;
    }
}
static const char *ht_trestle_caption(const ht_game *g,int line) {
    int p=g->traversal.trestle_phase;
    if(p<80)return line?"WITH THE TRACK ABOVE MY HEAD.":"I HAD TO CLIMB DOWN TO THE BRACING.";
    if(p<HT_TRESTLE_TOUCH)return line?"BEHIND ME, PALE PALM PRINTS DIMINISHED.":"I FELT EACH JOINT BEFORE TRUSTING IT.";
    if(p<HT_TRESTLE_TOUCH+1)return line?"THIS TIME I DID NOT LIFT IT TO MY FACE.":"A STRIP OF WOOL WOUND AROUND A SPLINTERED BRACE.";
    if(p<HT_TRESTLE_TEST_END)return line?"":"I TESTED THE NEXT FOOTING FIRST.";
    return line?"BROKEN TIMBERS LAY BELOW.":"SLEEPERS BARRED THE SKY ABOVE.";
}
