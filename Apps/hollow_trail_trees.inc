/* The original scenery is the geometry: no added trees, limbs or platforms.
 * Keep these seed/parcel choices identical to ht_draw_traversal. Background
 * parallax trees are deliberately not in the player's physical depth plane. */
typedef struct {int x,base,height,width;unsigned seed;} ht_climb_tree;
typedef struct {int x,y,mx,my,ex,ey,r,mr,side;} ht_tree_branch;
static ht_tree_branch ht_tree_branch_shape(int x,int base,int height,int width,unsigned seed,int j) {
    if(j==5 && seed==1917) {
        const ht_mechanics *m=&ht_mechanics_by_level[0];
        return (ht_tree_branch){x,m->rope_anchor_y-28,m->rope_anchor_x-35,m->rope_anchor_y-19,
            m->rope_anchor_x+16,m->rope_anchor_y-12,9,6,1};
    }
    unsigned h=ht_hash(seed),b=ht_hash(seed+(unsigned)j*137u);
    int lean=(int)(h%43)-21,bend=(int)((h>>8)%19)-9;
    int kx=x+lean/3+bend,tx=x+lean;
    int rise=30+j*12+(int)((b>>8)%9),at=base-height*rise/100;
    int root=rise<52?x+(kx-x)*rise/52:kx+(tx-kx)*(rise-52)/48;
    int side=(b&1u)?1:-1,reach=height*(15+(int)((b>>12)%20))/100;
    int ex=root+side*reach,ey=at-height*(4+(int)((b>>16)%17))/100;
    return (ht_tree_branch){root,at,root+(ex-root)*55/100,at+(ey-at)/3,ex,ey,
        ht_max(1,width*(8-j)/13),ht_max(1,width/4),side};
}
static bool ht_existing_tree_geometry(const ht_game *g,int i,ht_climb_tree *out) {
    int world,height,width,parcel=i;unsigned seed=(unsigned)i+901u;
    if(i==HT_PLATFORMS && g->level==0) {
        world=ht_mechanics_by_level[0].rope_anchor_x-96;height=295;width=16;parcel=4;seed=1917;
    } else {
        if(i>=HT_PLATFORMS || i==ht_mechanics_by_level[g->level].bridge ||
           (i!=0 && i!=2 && i!=5 && i!=8) || (g->level==4 && i==0) ||
           (g->level!=0 && g->level!=4 && g->level!=6 && g->level!=8)) return false;
        const ht_platform *p=&ht_level_land(g->level)[i];
        world=i==0?15:p->left+(p->right-p->left)*2/3;
        height=g->level==0?340+(i%3)*65:130+(i%3)*27;
        width=g->level==0?23+(i%3)*5:7;
    }
    int base=ht_surface_at(g,parcel,world),buried=ht_buried_base(g,parcel,world,width);
    *out=(ht_climb_tree){world,buried,height+buried-base,width,seed};return true;
}
/* All these trees stand on immutable land (never the moving bridge).
 * Cache the small geometry table rather than resampling root soil at input Hz. */
static bool ht_existing_tree(const ht_game *g,int i,ht_climb_tree *out) {
    static unsigned level=HT_LEVELS,valid;
    static ht_climb_tree trees[HT_PLATFORMS+1];
    if(level!=g->level) {
        valid=0;
        for(int n=0;n<=HT_PLATFORMS;++n)
            if(ht_existing_tree_geometry(g,n,&trees[n]))valid|=1u<<n;
        level=g->level;
    }
    if(i<0 || i>HT_PLATFORMS || !(valid&(1u<<i)))return false;
    *out=trees[i];return true;
}
/* Follow the tapered, bent trunk where the character's hands meet it. */
static int ht_tree_edge(const ht_climb_tree *tree,int feet,int side) {
    unsigned h=ht_hash(tree->seed);
    int lean=(int)(h%43)-21,bend=(int)((h>>8)%19)-9;
    int kx=tree->x+lean/3+bend,tx=tree->x+lean;
    int d=ht_clamp(tree->base-(feet-20),0,tree->height),k=tree->height*52/100;
    int mid=ht_max(2,tree->width*2/3),center,radius;
    if(d<=k) {center=tree->x+(kx-tree->x)*d/ht_max(1,k);radius=tree->width+(mid-tree->width)*d/ht_max(1,k);}
    else {center=kx+(tx-kx)*(d-k)/ht_max(1,tree->height-k);radius=mid+(1-mid)*(d-k)/ht_max(1,tree->height-k);}
    return center+side*(radius+5);
}
static int ht_tree_near(const ht_game *g) {
    for(int i=0;i<=HT_PLATFORMS;++i) {
        ht_climb_tree tree;if(!ht_existing_tree(g,i,&tree))continue;
        if(g->y/256<tree.base-tree.height+20 || g->y/256>tree.base+3)continue;
        for(int side=-1;side<=1;side+=2)
            if(ht_abs(g->x/256-ht_tree_edge(&tree,g->y/256,side))<=6)return i;
    }
    return -1;
}
/* Footing is on the upper edge of the same two tapered limb polygons.
 * Fine twigs and the thin outer tip are scenery, not a platform network. */
static bool ht_tree_footing(const ht_climb_tree *tree,int branch,int x,int *top) {
    if(branch==5 && tree->seed!=1917)return false;
    ht_tree_branch b=ht_tree_branch_shape(tree->x,tree->base,tree->height,tree->width,tree->seed,branch);
    int dx=(x-b.x)*b.side,mid=(b.mx-b.x)*b.side,reach=(b.ex-b.x)*b.side;
    if(dx<0 || dx>reach*3/4)return false;
    int y,r;
    if(dx<=mid) {y=b.y+(b.my-b.y)*dx/ht_max(1,mid);r=b.r+(b.mr-b.r)*dx/ht_max(1,mid);}
    else {y=b.my+(b.ey-b.my)*(dx-mid)/ht_max(1,reach-mid);r=b.mr+((branch==5?2:0)-b.mr)*(dx-mid)/ht_max(1,reach-mid);}
    *top=y-r;return true;
}
