/* Authored living trees: one definition drives art, grip and branch footing.
 * Trunks occupy the depth plane immediately behind the walkable route; limbs
 * are one-way surfaces, so the existing ground route remains traversable. */
typedef struct { int16_t x,height; uint8_t parcel,radius; } ht_living_tree;
static const ht_living_tree ht_forest_trees[]={
    {120,365,0,24},{910,420,2,30},{1120,375,3,25},
    {1900,430,5,32},{2630,390,8,28}
};
static const ht_living_tree ht_marsh_trees[]={{1130,300,2,18},{2230,340,6,23}};
static const ht_living_tree ht_garden_trees[]={{180,340,0,23},{1500,360,4,25},{2620,320,8,22}};
static const ht_living_tree ht_ridge_trees[]={{170,300,0,21},{1540,330,4,24},{2650,310,8,23}};
static const ht_living_tree *ht_trees(const ht_game *g,int *count) {
    switch(g->level) {
    case 0:*count=5;return ht_forest_trees;
    case 4:*count=2;return ht_marsh_trees;
    case 6:*count=3;return ht_garden_trees;
    case 8:*count=3;return ht_ridge_trees;
    default:*count=0;return ht_forest_trees;
    }
}
static int ht_tree_base(const ht_game *g,const ht_living_tree *tree) {
    return ht_surface_at(g,tree->parcel,tree->x);
}
#define HT_TREE_BRANCHES 5
static int ht_branch_side(int branch) {return branch%2?-1:1;}
static void ht_branch_bounds(const ht_living_tree *tree,int branch,int *l,int *r) {
    int reach=110-branch*9,side=ht_branch_side(branch);
    *l=tree->x+(side<0?-reach:tree->radius-6);
    *r=tree->x+(side>0?reach:-tree->radius+6);
}
static int ht_branch_top(const ht_game *g,const ht_living_tree *tree,int branch,int x) {
    int distance=ht_abs(x-tree->x),rise=ht_max(0,distance-tree->radius)/9;
    return ht_tree_base(g,tree)-58-branch*(tree->height-100)/HT_TREE_BRANCHES-rise;
}
static int ht_tree_near(const ht_game *g) {
    int count;const ht_living_tree *trees=ht_trees(g,&count);
    for(int i=0;i<count;++i) {
        int base=ht_tree_base(g,&trees[i]);
        if(ht_abs(ht_abs(g->x/256-trees[i].x)-(trees[i].radius+6))<=13 &&
           g->y/256>=base-trees[i].height+35 && g->y/256<=base+5)return i;
    }
    return -1;
}
