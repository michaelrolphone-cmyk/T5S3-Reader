/* Input-only chapter witness: follow each authored platform connection and
 * operate its actual mass, plate, boat, ladder or rope. Never teleport bodies,
 * move fixtures, set puzzle/bridge flags or bypass normal simulation. */
/* Independent witness actions, not solutions read from production targets. */
static const char *walk_solutions[HT_LEVELS]={
    "1123","001223","01210123","0201012010103","03311332","11111223","0122232223","000301113","31021","000"
};
static unsigned walk_phase,walk_level=999,walk_mechanics,walk_task,walk_puzzle;
static void walk_route_tick(void) {
    if(walk_level!=ht.level) {walk_level=ht.level;walk_phase=0;walk_mechanics=walk_task=walk_puzzle=0;}
    int x=ht.x/256,y=ht.y/256,dir=0,vertical=0;
    bool jump=false,mass_work=false;
    const ht_mechanics *m=ht_mech(&ht);ht_traversal *t=&ht.traversal;
    if(ht.level==0 && (t->mode==HT_SHUTTER || (x>=1084 && x<1111))) {
        if(!t->mode && ht_mill_shutter_near(&ht)>0){assert(ht_traversal_interact());walk_mechanics|=64;}
        ht_step_controls(1,0,false,false);
        if(!t->mode && ht.grounded && ht.x>=1111*256 && walk_phase==2){walk_phase=3;walk_task=0;}
        return;
    }
    if(ht.level==1 && (t->mode==HT_WINDOW || (x>=HT_SIGNAL_WINDOW_LEFT-5 && x<HT_SIGNAL_WINDOW_RIGHT+5 && y==ht_signal_window_floor()))) {
        if(!t->mode && ht_signal_window_near(&ht)>0){assert(ht_traversal_interact());walk_mechanics|=64;}
        ht_step_controls(1,0,false,false);return;
    }
    if(ht.level==3 && (t->mode==HT_TRESTLE || (!t->mode && x>=1232 && x<1490))) {
        if(!t->mode && ht_trestle_near(&ht)){assert(ht_traversal_interact());walk_mechanics|=64;}
        if(t->mode==HT_TRESTLE && t->trestle_phase==HT_TRESTLE_TOUCH && !t->trestle_tested)
            assert(ht_traversal_interact());
        ht_step_controls(1,0,false,false);
        if(!t->mode && ht.grounded && ht.x>=1490*256){walk_phase=3;walk_task=0;}
        return;
    }
    /* The mill register has to be physically brought into the window beam.
     * This witness uses the same A grip and horizontal input as a player. */
    if(ht.level==0 && walk_phase==3 && !ht_evidence_found(&ht,1) && !ht_mill_register_lit(&ht)) {
        int desk=t->crate_x/256;
        if(t->mode!=HT_CRATE) {
            if(x<desk-19)dir=1;
            else if(x>desk-15) {dir=-1;jump=ht.grounded;}
            else if(ht.grounded) {assert(ht_traversal_interact());assert(t->mode==HT_CRATE);}
        }
        if(t->mode==HT_CRATE)dir=1;
        ht_step_controls(dir,0,jump,true);return;
    }
    int evidence=ht_evidence_near(&ht);if(evidence>=0) assert(ht_inspect()>=0);
    if(t->mode==HT_LEDGE) {ht_step_controls(0,-1,false,true);return;}
    if(t->mode==HT_CLIMB) {ht_step_controls(0,0,false,true);return;}
    const ht_platform *here=&ht_land[walk_phase];
    if(walk_phase<9) {
        const ht_platform *next=&ht_land[walk_phase+1];
        if(ht.grounded && y==ht_surface_at(&ht,(int)walk_phase+1,x) && x>=next->left+5 && x<next->right) {
            ++walk_phase;walk_task=0;here=&ht_land[walk_phase];
            next=walk_phase<9?&ht_land[walk_phase+1]:here;
        }
        /* Evidence on return shelves requires a deliberate westward detour. */
        for(int item=0;item<3;++item) if((unsigned)ht_evidence_platform(ht.level,item)==walk_phase &&
           !ht_evidence_found(&ht,ht.level*3+(unsigned)item)) {
            int target=ht_evidence_x(ht.level,item);
            dir=x<target?1:-1;
            bool obstacle=(m->ball_x && ht_abs(ht.x-t->ball_x)<(HT_BALL_RADIUS+12)*256) ||
                (m->crate_x && ht_abs(ht.x-t->crate_x)<(HT_CRATE_HALF+12)*256);
            ht_step_controls(dir,0,ht.grounded && obstacle,true);return;
        }
        if(walk_phase<9) {
            int ladder=-1;
            for(int i=0;i<3;++i) if(m->ladders[i].x && m->ladders[i].lower==(int)walk_phase &&
                 m->ladders[i].upper==(int)walk_phase+1) ladder=i;
            bool gate=(int)walk_phase+1==m->bridge && t->bridge_open<32;
            bool crate_step=ladder<0 && next->top<here->top-70;
            bool fill_pit=m->ball_x && m->pit_platform==(int)walk_phase;
            if(ht.level==0 && gate) {
                if(x<350)dir=1;
                else if(!t->forest_log_falling) {
                    if(t->mode!=HT_PUSH){assert(ht_traversal_interact());walk_mechanics|=32;}
                    dir=1;
                }
            } else if(ht.level==0 && walk_phase==0) {
                dir=1;if(ht.grounded && x>=360)jump=true;
            } else if(gate || crate_step || fill_pit) {
                mass_work=true;
                int kind=fill_pit?1:gate?m->plate_kind:2;
                int object=(kind==1?t->ball_x:t->crate_x)/256;
                int target=fill_pit?m->pit_x:gate?m->plate_x:next->left-28;
                int reach=kind==1?HT_BALL_RADIUS+7:HT_CRATE_HALF+7;
                if(walk_task==0 && object<target-2) {
                    if(t->mode==HT_FREE && (x>=object || (y<(kind==1?t->ball_y/256+HT_BALL_RADIUS:t->crate_y/256)-5 && x>object-reach-10))) {dir=-1;jump=ht.grounded;}
                    else if(t->mode==HT_FREE && x<object-reach) dir=1;
                    else if(t->mode==HT_FREE && ht.grounded) {assert(ht_traversal_interact());assert(t->mode==(kind==1?HT_ROLL:HT_CRATE));}
                    if(t->mode==HT_ROLL || t->mode==HT_CRATE) {dir=1;walk_mechanics|=kind==1?1:2;}
                } else if(fill_pit) {
                    if(t->mode)assert(ht_traversal_interact());
                    dir=1;jump=ht.grounded;
                } else if(gate) {
                    if(t->mode) assert(ht_traversal_interact());
                } else {
                    if(!walk_task) {jump=true;dir=1;walk_task=1;}
                    else if(ht.grounded && y==t->crate_y/256-HT_CRATE_HEIGHT) {jump=true;dir=1;walk_task=2;}
                    else dir=walk_task==2 || x<object?1:0;
                }
            } else if(ladder>=0) {
                int target=m->ladders[ladder].x;
                if(t->mode==HT_LADDER) {vertical=-1;walk_mechanics|=4;}
                else if(ht_abs(x-target)>8) dir=x<target?1:-1;
                else vertical=-1;
            } else if(m->water_left==here->right && m->water_right==next->left) {
                if(walk_task==4) {dir=1;if(t->support==3 && ht.grounded) jump=true;}
                else if(t->mode==HT_BOAT) {
                    if(t->boat_x>=m->boat_right*256) {assert(ht_traversal_interact());dir=1;walk_task=4;}
                    else {dir=1;walk_mechanics|=16;}
                } else if(x<m->boat_left-35) dir=1;
                else if(x<m->water_right) {
                    if(ht_traversal_near(&ht)==HT_BOAT) {assert(ht_traversal_interact());assert(t->mode==HT_BOAT);}
                    dir=1;
                }
                else dir=1;
            } else if(next->left-here->right>60 && m->rope_anchor_x>here->left && m->rope_anchor_x<here->right) {
                if(walk_task==3) dir=1;
                else if(t->mode==HT_ROPE) {
                    dir=t->rope_v>=0?1:-1;walk_mechanics|=8;
                    if(x>=m->rope_anchor_x+24 && t->rope_v>600) {jump=true;walk_task=3;}
                } else if(x<m->rope_anchor_x-6) dir=1;
                else if(ht_traversal_near(&ht)==HT_ROPE) assert(ht_traversal_interact());
            } else {
                dir=1;
                int takeoff=next->top<here->top?ht_min(here->right-6,next->left-15):here->right-6;
                if(ht.grounded && (next->left-here->right>12 || (next->left>here->right && next->top<=here->top) || next->top<here->top) && x>=takeoff) jump=true;
            }
        }
    }
    if(walk_phase==9) {
        dir=1;
        if(x>=2820 && ht.grounded && !ht.puzzle.solved) {
            assert(walk_puzzle<strlen(walk_solutions[ht.level]));
            int station=walk_solutions[ht.level][walk_puzzle]-'0';
            int target=HT_PUZZLE_FIRST+station*HT_PUZZLE_SPACING;
            dir=x<target?1:-1;
            if(ht_puzzle_near(&ht)==station) {assert(ht_interact());++walk_puzzle;dir=0;}
        }
        if(ht_final_near(&ht) && !ht.verdict) {assert(ht_decide(1));ht.verdict_read=true;}
    }
    if(dir && ht.grounded && !t->mode && !walk_task && !mass_work) {
        if(m->ball_x && ht_abs(ht.x-t->ball_x)<(HT_BALL_RADIUS+12)*256 &&
           ht_abs(ht.y-(t->ball_y+HT_BALL_RADIUS*256))<4*256) jump=true;
        if(m->crate_x && ht_abs(ht.x-t->crate_x)<(HT_CRATE_HALF+12)*256 && ht_abs(ht.y-t->crate_y)<4*256) jump=true;
    }
    ht_step_controls(dir,vertical,jump,true);
}
