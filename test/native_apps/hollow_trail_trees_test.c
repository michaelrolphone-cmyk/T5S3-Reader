#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY);assert(memory);ht_bind(memory);
    unsigned branches=0;
    for(unsigned level=0;level<HT_LEVELS;++level) {
        ht.level=level;ht_spawn(true);int count;const ht_living_tree *trees=ht_trees(&ht,&count);
        for(int i=0;i<count;++i)for(int b=0;b<HT_TREE_BRANCHES;++b) {
            ht_spawn(true);const ht_living_tree *tree=&trees[i];int side=ht_branch_side(b);
            ht.x=(tree->x+side*(tree->radius+6))*256;ht.y=ht_tree_base(&ht,tree)*256;
            ht.grounded=true;assert(ht_tree_near(&ht)==i);assert(ht_traversal_interact());assert(ht.traversal.mode==HT_TREE);
            int target=ht_branch_top(&ht,tree,b,ht.x/256);
            for(int step=0;step<250 && ht.y>(target+2)*256;++step)ht_step_controls(0,-1,false,false);
            assert(ht_abs(ht.y-target*256)<=3*256);
            ht_step_controls(side,0,false,false);assert(ht.grounded && ht.traversal.mode==HT_FREE && ht.traversal.support==5);
            for(int step=0;step<8;++step)ht_step_controls(side,0,false,false);
            assert(ht.grounded && ht.traversal.support==5);
            assert(ht.y==ht_branch_top(&ht,tree,b,ht.x/256)*256);
            int before=ht.y;ht_step_controls(0,1,false,false);
            assert(!ht.grounded && ht.y>before && ht.traversal.mode==HT_FREE);
            /* Descending onto the same limb catches; ascending passes through. */
            ht.traversal.drop_cooldown=ht.traversal.branch_drop=0;int x=tree->x+side*(tree->radius+20);
            int top=ht_branch_top(&ht,tree,b,x);ht.x=x*256;ht.y=(top-2)*256;ht.vy=900;
            ht_step_controls(0,0,false,false);assert(ht.grounded && ht.y==top*256);
            ht.y=(top+5)*256;ht.vy=-1000;ht.grounded=false;
            ht_step_controls(0,0,false,true);assert(!ht.grounded && ht.vy<0);
            ++branches;
        }
        if(count) {
            ht_spawn(true);int base=ht_tree_base(&ht,&trees[0]);ht.x=(trees[0].x-trees[0].radius-6)*256;ht.y=base*256;
            ht_step_controls(0,-1,false,false);assert(ht.traversal.mode==HT_TREE);
            for(int n=0;n<280;++n)ht_step_controls(0,-1,false,false);
            assert(ht.y==(base-trees[0].height+35)*256);
            int y=ht.y;ht_step_controls(0,0,true,true);
            assert(ht.traversal.mode==HT_FREE && ht.y<y && ht.vx<0);
            assert(!ht_tree_enter()); // No immediate regrab after jumping.
        } else assert(ht_tree_near(&ht)==-1);
    }
    assert(branches==65);free(memory);
    puts("Living trees: 65 shared branch surfaces, grip, ascent, step-out, slope walking, drop-through, landing and jump release PASS");
}
