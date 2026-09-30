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
/* The supporting limb and its foothold follow the same loaded attachment. */
static ht_tree_branch ht_rope_branch(const ht_game *g,int trunk) {
    ht_tree_branch b=ht_tree_branch_shape(trunk,0,0,0,1917,5);
    int dx=g->traversal.rope_bend_x/256,dy=g->traversal.rope_bend_y/256;
    b.mx+=dx/2;b.my+=dy/2;b.ex+=dx;b.ey+=dy;
    return b;
}
/* Bank adjustments never re-space the established living trees. */
static int ht_planted_tree_x(unsigned level,int i) {
    static const int16_t roots[HT_LEVELS][4]={
        {15,943,1893,2690},{0},{0},{0},{15,1173,2033,2743},
        {0},{15,963,1903,2706},{0},{15,966,1843,2730},{0}
    };
    int slot=i==0?0:i==2?1:i==5?2:3;
    if(roots[level][slot])return roots[level][slot];
    const ht_platform *p=&ht_level_land(level)[i];
    return i==0?15:p->left+(p->right-p->left)*2/3;
}
static bool ht_existing_tree_geometry(const ht_game *g,int i,ht_climb_tree *out) {
    int world,height,width,parcel=i;unsigned seed=(unsigned)i+901u;
    if(i==HT_PLATFORMS && g->level==0) {
        world=ht_mechanics_by_level[0].rope_anchor_x-96;height=295;width=16;parcel=4;seed=1917;
    } else {
        if(i>=HT_PLATFORMS || i==ht_mechanics_by_level[g->level].bridge ||
           (i!=0 && i!=2 && i!=5 && i!=8) || (g->level==4 && i==0) ||
           (g->level!=0 && g->level!=4 && g->level!=6 && g->level!=8)) return false;
        world=ht_planted_tree_x(g->level,i);
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
/* Sixty-four paired sections describe a continuous asymmetric trunk. Slow
 * growth bends, bark ridges and two local burls have different spatial scales.
 * The renderer and hand contacts interpolate these exact same vertices. */
static void ht_organic_trunk_node(const ht_climb_tree *t,int node,int *center,int *left,int *right) {
    static const int8_t bends[17]={0,-1,-3,-2,0,3,5,4,1,-2,-4,-1,2,4,2,0,-1};
    static const uint8_t girth[17]={138,115,103,98,100,94,89,85,77,73,65,58,47,39,28,16,4};
    int q=ht_min(15,node/4),u=node-q*4;
    unsigned h=ht_hash(t->seed),b=ht_hash(t->seed+(unsigned)node*71u);
    int lean=(int)(h%43)-21;
    *center=t->x+lean*node/64+(bends[q]*(4-u)+bends[q+1]*u)*t->width/64;
    int radius=ht_max(1,t->width*(girth[q]*(4-u)+girth[q+1]*u)/400);
    int bark=ht_max(1,t->width/16),fade=64-node;
    int burl1=ht_max(0,5-ht_abs(node-(19+(int)(h%8))));
    int burl2=ht_max(0,4-ht_abs(node-(40+(int)((h>>6)%7))));
    *left=radius+((int)(b%3)-1)*bark*fade/64+burl1*t->width/17;
    *right=radius+((int)((b>>8)%3)-1)*bark*fade/64+burl2*t->width/15;
    *left=ht_max(1,*left);*right=ht_max(1,*right);
}
static int ht_organic_trunk_edge(const ht_climb_tree *t,int d,int side) {
    int node=ht_min(63,d*64/ht_max(1,t->height));
    while(node<63 && t->height*(node+1)/64<d)++node;
    int y0=t->height*node/64,y1=t->height*(node+1)/64;
    int c0,l0,r0,c1,l1,r1;
    ht_organic_trunk_node(t,node,&c0,&l0,&r0);
    ht_organic_trunk_node(t,node+1,&c1,&l1,&r1);
    int a=c0+(side<0?-l0:r0),b=c1+(side<0?-l1:r1);
    return a+(b-a)*(d-y0)/ht_max(1,y1-y0);
}
/* Curved substantial limbs share their upper silhouette with branch footing. */
static void ht_organic_branch_node(const ht_tree_branch *b,int node,int *x,int *y,int *radius) {
    static const int8_t bend[13]={0,1,3,2,-1,-3,-1,2,3,1,-2,-1,0};
    int q=ht_min(11,node/2),u=node-q*2;
    int reach=ht_abs(b->ex-b->x),at=reach*node/24,mid=ht_abs(b->mx-b->x);
    *x=b->x+b->side*at;
    if(at<=mid) {*y=b->y+(b->my-b->y)*at/ht_max(1,mid);*radius=b->r+(b->mr-b->r)*at/ht_max(1,mid);}
    else {*y=b->my+(b->ey-b->my)*(at-mid)/ht_max(1,reach-mid);*radius=b->mr-b->mr*(at-mid)/ht_max(1,reach-mid);}
    *y+=(bend[q]*(2-u)+bend[q+1]*u)*ht_max(1,b->r/6)/2;
    *radius+=ht_max(0,3-ht_abs(node-7))*b->r/12;
}
/* Follow the tapered, bent trunk where the character's hands meet it. */
static int ht_tree_edge(const ht_climb_tree *tree,int feet,int side) {
    if(tree->height>=260) {
        int d=ht_clamp(tree->base-(feet-20),0,tree->height);
        return ht_organic_trunk_edge(tree,d,side)+side*5;
    }
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
static bool ht_tree_footing(const ht_game *g,const ht_climb_tree *tree,int branch,int x,int *top) {
    if(branch==5 && tree->seed!=1917)return false;
    ht_tree_branch b=ht_tree_branch_shape(tree->x,tree->base,tree->height,tree->width,tree->seed,branch);
    if(branch==5 && tree->seed==1917)b=ht_rope_branch(g,tree->x);
    int dx=(x-b.x)*b.side,mid=(b.mx-b.x)*b.side,reach=(b.ex-b.x)*b.side;
    if(dx<0 || dx>reach*3/4)return false;
    if(tree->height>=260 && branch!=5) {
        int node=ht_min(23,dx*24/ht_max(1,reach));
        while(node<23 && reach*(node+1)/24<dx)++node;
        int x0,y0,r0,x1,y1,r1;
        ht_organic_branch_node(&b,node,&x0,&y0,&r0);
        ht_organic_branch_node(&b,node+1,&x1,&y1,&r1);
        int a=(x0-b.x)*b.side,z=(x1-b.x)*b.side;
        *top=y0-r0+((y1-r1)-(y0-r0))*(dx-a)/ht_max(1,z-a);return true;
    }
    int y,r;
    if(dx<=mid) {y=b.y+(b.my-b.y)*dx/ht_max(1,mid);r=b.r+(b.mr-b.r)*dx/ht_max(1,mid);}
    else {y=b.my+(b.ey-b.my)*(dx-mid)/ht_max(1,reach-mid);r=b.mr+((branch==5?2:0)-b.mr)*(dx-mid)/ht_max(1,reach-mid);}
    *top=y-r;return true;
}
