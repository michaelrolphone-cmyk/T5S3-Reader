/* Regression witnesses for post-climb and post-prop terrain penetration. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static unsigned checked;
static void clear_ground(void) {
    for(int i=0;i<HT_PLATFORMS;++i)for(int part=0;part<3;++part) {
        int l,r,top;if(!ht_platform_piece(&ht,i,part,&l,&r,&top))break;
        if(ht.traversal.mode==HT_LADDER && i==ht_mech(&ht)->ladders[ht.traversal.ladder].upper)continue;
        int x=ht.x/256;
        if(x<l || x>=r)continue;
        if(part!=2)top=ht_surface_at(&ht,i,x);
        int bottom=ht_terrain_bottom(&ht,i,x);
        /* Independently inspect occupied world rows, including eroded cliffs.
         * A foot exactly on a surface is contact, not occupied ground. */
        int first=ht_max(top,ht_floor_div(ht.y-29*256,256)),last=ht_min(bottom,-ht_floor_div(-ht.y,256));
        for(int y=first;y<last;++y) {
            int left=l,right=r;
            if(part==0) {
                if(ht_cliff_edge(&ht,i,-1))left+=ht_cliff_inset(&ht,i,-1,y);
                if(ht_cliff_edge(&ht,i,1))right-=ht_cliff_inset(&ht,i,1,y);
            }
            if(x>=left && x<right) {
                fprintf(stderr,"embedded level%u parcel%d part%d mode%u x%d feet%d.%u soil%d row%d\n",
                    ht.level,i,part,ht.traversal.mode,x,ht.y/256,(unsigned)ht.y&255u,top,y);
                abort();
            }
        }
    }
    ++checked;
}
static void step(int direction,int vertical,bool jump) {
    ht_step_controls(direction,vertical,jump,jump);clear_ground();
}
static void stone_edges(void) {
    ht.level=0;ht_spawn(true);ht.x=189*256;ht.y=ht_surface_at(&ht,0,189)*256;
    assert(ht_traversal_interact() && ht.traversal.mode==HT_ROLL);
    for(int n=0;n<200 && ht.traversal.ball_x<282*256;++n)step(1,0,false);
    if(ht.traversal.mode==HT_ROLL)assert(ht_traversal_interact());
    for(int n=0;n<80;++n)step(0,0,false);
    assert(ht.traversal.ball_y+HT_BALL_RADIUS*256>240*256);
    ht_game settled=ht;
    for(int side=-1;side<=1;side+=2)for(int jump=0;jump<2;++jump) {
        ht=settled;ht.x=ht.traversal.ball_x+side*45*256;
        ht.y=ht_surface_at(&ht,0,ht.x/256)*256;
        ht.vx=ht.vy=0;ht.grounded=true;ht.traversal.support=0;
        for(int n=0;n<130 && ht.x<345*256 && ht.x>230*256;++n)step(-side,0,jump && n==20);
        for(int n=0;n<35 && ht.x<345*256 && ht.x>230*256;++n)step(side,0,false);
        assert(ht.deaths==settled.deaths);
    }
}
static void tree_bases(void) {
    unsigned trees=0;
    for(unsigned level=0;level<HT_LEVELS;++level)for(int id=0;id<=HT_PLATFORMS;++id)
        for(int side=-1;side<=1;side+=2) {
            ht.level=level;ht_spawn(true);ht_climb_tree tree;
            if(!ht_existing_tree(&ht,id,&tree))continue;
            ht.y=(tree.base-60)*256;ht.x=ht_tree_edge(&tree,ht.y/256,side)*256;
            assert(ht_tree_enter(0,-1,false));
            for(int n=0;n<170;++n)step(0,1,false);
            assert(ht.traversal.mode==HT_FREE && ht.grounded && !ht.deaths);
            /* Leaving and jumping from the root cannot re-open the floor. */
            for(int n=0;n<35;++n)step(side,0,n==4);
            assert(!ht.deaths);++trees;
        }
    assert(trees==32);
}
static void terrain_transitions(void) {
    /* Walk/jump both ways across every authored surface, including sockets,
     * ledge catches and thin overhead routes. Actual gaps may still be fallen. */
    for(unsigned level=0;level<HT_LEVELS;++level)for(int i=0;i<HT_PLATFORMS;++i)
        for(int side=-1;side<=1;side+=2) {
            ht.level=level;ht_spawn(true);
            if(level==0){ht.traversal.forest_log_phase=32;ht.traversal.bridge_open=32;}
            int l,r,top;assert(ht_platform_piece(&ht,i,0,&l,&r,&top));
            ht.x=(side<0?r-18:l+18)*256;ht.y=ht_surface_at(&ht,i,ht.x/256)*256;
            /* An overlapping higher bank may cover this test start. */
            ht_reconcile_ground(ht.x,ht.y);
            for(int n=0;n<90;++n)step(side,0,n==16 || n==60);
        }
    /* A low branch that overlaps a higher bank must yield to the ground. */
    ht.level=6;ht_spawn(true);ht_climb_tree tree;assert(ht_existing_tree(&ht,5,&tree));
    ht_tree_branch limb=ht_tree_branch_shape(tree.x,tree.base,tree.height,tree.width,tree.seed,2);
    int top;assert(ht_tree_footing(&tree,2,limb.mx,&top));
    ht.x=limb.mx*256;ht.y=(top-2)*256;ht.vy=900;ht.grounded=false;
    for(int n=0;n<40;++n)step(1,0,false);
    /* An open ravine remains empty; recovery is not an invisible floor. */
    ht.level=0;ht_spawn(true);ht.x=520*256;ht.y=250*256;ht.grounded=false;
    for(int n=0;n<12;++n)step(0,0,false);
    assert(ht.y>270*256 && !ht.grounded);
}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY);assert(memory);ht_bind(memory);
    stone_edges();tree_bases();terrain_transitions();free(memory);
    printf("Ground integrity: stone-hole edges, 32 tree descents, all chapter surfaces and real gaps PASS (%u poses)\n",checked);
}
