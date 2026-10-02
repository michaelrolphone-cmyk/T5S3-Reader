#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static void approach(int id,int side) {
    ht_spawn(true);ht_climb_tree tree;assert(ht_existing_tree(&ht,id,&tree));
    ht.y=(tree.base-3)*256;
    /* Drawing bases are buried; approach from actual soil, never inside it. */
    for(int n=0;n<4;++n) {
        ht.x=ht_tree_edge(&tree,ht.y/256,side)*256;
        ht.y=ht_min(ht.y,ht_surface_at(&ht,id==HT_PLATFORMS?4:id,ht.x/256)*256);
    }
    ht.grounded=true;ht.vx=ht.vy=0;
}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY);assert(memory);ht_bind(memory);
    unsigned counts[HT_LEVELS]={5,0,0,0,3,0,4,0,4,0};unsigned visited=0;
    for(unsigned level=0;level<HT_LEVELS;++level) {
        ht.level=level;ht_spawn(true);unsigned count=0;
        for(int i=0;i<=HT_PLATFORMS;++i) {
            ht_climb_tree tree;if(!ht_existing_tree(&ht,i,&tree))continue;++count;
            for(int side=-1;side<=1;side+=2) {
                approach(i,side);assert(ht_tree_near(&ht)==i);
                assert(!ht_tree_enter(1,-1,false)); // diagonal traversal never captures
                assert(!ht_tree_enter(0,1,false)); // down never captures
                assert(!ht_tree_enter(0,-1,true)); // held jump never captures
                (void)ht_traversal_interact();assert(ht.traversal.mode!=HT_TREE);
                approach(i,side);
                ht_step_controls(0,-1,false,false);assert(ht.traversal.mode==HT_TREE);
                int old=ht.y;ht_step_controls(0,-1,false,false);assert(ht.y<old);
                assert(!ht_traversal_interact());assert(ht.traversal.mode==HT_TREE);
                ht_step_controls(0,1,false,false);assert(ht.y>=old);
                ht_step_controls(side,0,false,false);assert(ht.traversal.mode==HT_FREE);
                // A sideways jump past a trunk must remain airborne and free.
                approach(i,side);ht.grounded=false;ht.y-=60*256;ht.vy=-700;ht.vx=side*640;
                for(int n=0;n<5;++n)ht_step_controls(side,0,n==0,true);
                assert(ht.traversal.mode!=HT_TREE);
                approach(i,side);ht_step_controls(0,-1,false,false);
                for(int n=0;n<700;++n)ht_step_controls(0,-1,false,false);
                assert(ht.traversal.mode==HT_TREE && ht.y==(tree.base-tree.height+20)*256);
                old=ht.y;ht_step_controls(side,0,true,true);
                assert(ht.traversal.mode==HT_FREE && ht.y<old && ht.vx*side>0);
                assert(!ht_tree_enter(0,-1,false)); // release cooldown
            }
            // Every substantial original limb supports landing/walking/drop;
            // ascending through its underside cannot attach to it or the trunk.
            for(int b=0;b<(tree.seed==1917?6:5);++b) {
                ht_tree_branch limb=ht_tree_branch_shape(tree.x,tree.base,tree.height,tree.width,tree.seed,b);
                int x=limb.mx,top;if(x<7 || x>HT_GOAL-7)continue;assert(ht_tree_footing(&ht,&tree,b,x,&top));
                /* A limb behind a higher bank is occluded by solid terrain;
                 * it must not provide a way to stand inside that bank. */
                bool buried=false;
                for(int land=0;land<HT_PLATFORMS;++land)for(int part=0;part<3;++part) {
                    int l,r,soil;if(!ht_platform_piece(&ht,land,part,&l,&r,&soil))break;
                    if(part!=2)soil=ht_surface_at(&ht,land,x);
                    if(x>=l && x<r && top>soil && top-29<ht_terrain_bottom(&ht,land,x))buried=true;
                }
                if(buried)continue;
                approach(i,limb.side);ht.x=x*256;ht.y=(top-2)*256;ht.vy=900;ht.grounded=false;
                ht_step_controls(0,0,false,false);assert(ht.grounded && ht.traversal.support==5);
                ht_step_controls(limb.side,0,false,false);assert(ht.grounded && ht.traversal.support==5);
                ht_step_controls(0,1,false,false);assert(!ht.grounded && ht.traversal.branch_drop);
                ht.traversal.branch_drop=0;ht.x=x*256;ht.y=(top+5)*256;ht.vy=-1000;
                ht_step_controls(0,0,false,true);assert(!ht.grounded && ht.vy<0 && ht.traversal.mode==HT_FREE);
                // Climb up the real trunk, then step onto this branch at its root.
                approach(i,limb.side);assert(ht_tree_enter(0,-1,false));
                bool stepped=false;
                for(int n=0;n<700 && !stepped;++n) {
                    int px=ht_tree_edge(&tree,ht.y/256,limb.side),at;
                    if(ht_tree_footing(&ht,&tree,b,px,&at) && ht_abs(ht.y/256-at)<=2) {
                        ht_step_controls(limb.side,0,false,false);
                        assert(ht.grounded && ht.traversal.support==5 && ht.traversal.mode==HT_FREE);stepped=true;
                    } else ht_step_controls(0,-1,false,false);
                }
                assert(stepped);++visited;
            }
        }
        assert(count==counts[level]);
    }
    free(memory);printf("Original trees: %u limb contacts; Up intent, no sideways/A capture, climb, step-out, jump and drop PASS\n",visited);
}
