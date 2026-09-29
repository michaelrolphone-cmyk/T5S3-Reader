/* Input-only route witness. Never teleports, sets object positions, unlocks
 * traversal flags or skips simulation. Each chapter must traverse all five
 * mechanisms and collect its three evidence records on the actual route. */
static unsigned walk_phase,walk_level=999,walk_mechanics;
static void walk_route_tick(void) {
    if(walk_level!=ht.level) {walk_level=ht.level;walk_phase=0;walk_mechanics=0;}
    int x=ht.x/256,y=ht.y/256,ground=ht_land[0].top,dir=0,vertical=0;
    bool jump=false;
    ht_traversal *t=&ht.traversal;
    int evidence=ht_evidence_near(&ht);if(evidence>=0) assert(ht_inspect()>=0);
    switch(walk_phase) {
    case 0:
        if(t->ball_phase) {walk_phase=1;walk_mechanics|=1;break;}
        if(!t->mode && x<185) dir=1;
        else if(!t->mode) {assert(ht_traversal_interact());assert(t->mode==HT_ROLL);}
        if(t->mode==HT_ROLL) dir=1;
        if(t->ball_phase) {walk_phase=1;dir=0;walk_mechanics|=1;}
        break;
    case 1:
        if(t->ball_phase<2) {dir=x>290?-1:0;break;}
        dir=1;
        if(ht.grounded && ((x>=290 && x<342) || (x>=426 && x<480))) jump=true;
        if(x>=500) walk_phase=2;
        break;
    case 2:
        dir=1;
        if(!t->mode && x>=622 && t->crate_x<735*256) {assert(ht_traversal_interact());assert(t->mode==HT_CRATE);}
        if(t->crate_x==735*256 && (t->mode==HT_CRATE || ht.grounded)) jump=true;
        if(ht.grounded && y==ht_land[2].top) {walk_phase=3;walk_mechanics|=2;}
        break;
    case 3:
        if(x<985) dir=1;else vertical=-1;
        if(ht.grounded && y==ht_land[3].top) {walk_phase=4;walk_mechanics|=4;}
        break;
    case 4:
        if(x<1120) dir=1;
        else if(t->mode!=HT_ROPE) {if(ht_traversal_near(&ht)==HT_ROPE) assert(ht_traversal_interact());}
        if(t->mode==HT_ROPE) {dir=1;if(x>=1340 && t->rope_v>0) {jump=true;walk_phase=5;walk_mechanics|=8;}}
        break;
    case 5:
        dir=1;
        if(ht.grounded && x>=1390 && y==ht_land[4].top) walk_phase=6;
        break;
    case 6:
        if(x<1585) dir=1;else vertical=1;
        if(ht.grounded && y==ground) walk_phase=7;
        break;
    case 7:
        if(x<1720) dir=1;
        else if(t->mode!=HT_BOAT) {assert(ht_traversal_interact());assert(t->mode==HT_BOAT);}
        if(t->mode==HT_BOAT) {
            dir=1;
            if(t->boat_x==2160*256) {assert(ht_traversal_interact());walk_phase=8;walk_mechanics|=16;}
        }
        break;
    case 8:
        if(ht.grounded && y==ht_land[7].top) {
            /* The upper shelf doubles back to a clue before heading east. */
            if(x>ht_evidence_x(ht.level,2)+12) dir=-1;
            else walk_phase=9;
        } else if(x<2175) dir=1;else vertical=-1;
        break;
    case 9:
        dir=1;if(ht.grounded && x>=2313 && x<2410) jump=true;
        if(ht.grounded && x>=2410) walk_phase=10;
        break;
    case 10:
        dir=1;
        if(x>=2820 && ht.grounded && !ht.puzzle.solved) {
            const ht_puzzle_definition *p=&ht_puzzles[ht.level];int station=0;
            if(p->kind==0) station=p->answer[ht.puzzle.progress];
            else while(station<2 && ht.puzzle.value[station]==p->answer[station]) ++station;
            int target=HT_PUZZLE_FIRST+station*HT_PUZZLE_SPACING;
            dir=x<target?1:-1;
            if(ht_puzzle_near(&ht)==station) {assert(ht_interact());dir=0;}
        }
        if(ht_final_near(&ht) && !ht.verdict) {assert(ht_decide(1));ht.verdict_read=true;}
        if(x>=HT_GOAL-10) assert(walk_mechanics==31);
        break;
    }
    if(t->mode==HT_LEDGE) {vertical=-1;jump=false;}
    ht_step_controls(dir,vertical,jump,true);
}
