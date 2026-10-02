/* Reference-guided geometry, not traced image pixels. Oak growth forms:
 * irregular girth, branch collars, crooked forks and broad root flare. */
static void ht_organic_branch_draw(uint8_t *b,const ht_tree_branch *limb,int tone) {
    if(!ht_organic_visible(b,ht_min(limb->x,limb->ex)-limb->r,
        ht_min(limb->y,limb->ey)-limb->r*2,ht_max(limb->x,limb->ex)+limb->r,
        ht_max(limb->y,limb->ey)+limb->r*2))return;
    int px,py,pr;ht_organic_branch_node(limb,0,&px,&py,&pr);
    for(int n=1;n<=24;++n) {
        int x,y,r;ht_organic_branch_node(limb,n,&x,&y,&r);
        ht_triangle(b,px,py-pr,px,py+pr,x,y-r,tone);
        ht_triangle(b,px,py+pr,x,y+r,x,y-r,tone);
        px=x;py=y;pr=r;
    }
}
static void ht_organic_twig(uint8_t *b,int x,int y,int ex,int ey,int radius,unsigned seed,int tone) {
    if(!ht_organic_visible(b,ht_min(x,ex)-ht_abs(ex-x)/3-12,ht_min(y,ey)-ht_abs(ey-y)/3-12,
        ht_max(x,ex)+ht_abs(ex-x)/3+12,ht_max(y,ey)+radius+3))return;
    int px=x,py=y;
    for(int n=1;n<=6;++n) {
        unsigned h=ht_hash(seed+(unsigned)n*61u);
        int sway=n==6?0:((int)(h%7)-3);
        int nx=x+(ex-x)*n/6+sway,ny=y+(ey-y)*n/6;
        ht_limb(b,px,py,nx,ny,ht_max(0,radius*(7-n)/6),ht_max(0,radius*(6-n)/6),tone);
        if(n==3 || n==5) {
            int side=(h&1u)?1:-1;
            int tipx=nx+side*(5+ht_abs(ex-x)/4),tipy=ny-ht_max(6,ht_abs(ey-y)/3);
            ht_limb(b,nx,ny,tipx,tipy,0,0,tone);
            ht_limb(b,tipx,tipy,tipx+side*4,tipy-5,0,0,tone);
        }
        px=nx;py=ny;
    }
}
static void ht_organic_tree(uint8_t *b,int x,int base,int height,int width,unsigned seed,int tone,bool crown) {
    ht_climb_tree t={x,base,height,width,seed};
    int px,pl,pr;ht_organic_trunk_node(&t,0,&px,&pl,&pr);
    int py=base;
    for(int n=1;n<=64;++n) {
        int nx,nl,nr,ny=base-height*n/64;ht_organic_trunk_node(&t,n,&nx,&nl,&nr);
        if(ht_organic_visible(b,ht_min(px-pl,nx-nl),ny,ht_max(px+pr,nx+nr),py)) {
        ht_triangle(b,px-pl,py,px+pr,py,nx-nl,ny,tone);
        ht_triangle(b,px+pr,py,nx+nr,ny,nx-nl,ny,tone);
        }
        px=nx;py=ny;pl=nl;pr=nr;
        if((n&15)==0)ht_checkpoint();
    }
    if(crown && ht_organic_visible(b,x-height,base-height*3/2,x+height,base-height/4+width))
    for(int j=0;j<5;++j) {
        ht_tree_branch limb=ht_tree_branch_shape(x,base,height,width,seed,j);
        ht_organic_branch_draw(b,&limb,tone);
        unsigned h=ht_hash(seed+(unsigned)j*137u);
        int reach=ht_abs(limb.ex-limb.x);
        ht_organic_twig(b,limb.mx,limb.my,limb.mx+limb.side*reach/3,limb.my-height/7,
            ht_max(1,width/5),h,tone);
        ht_organic_twig(b,limb.ex,limb.ey,limb.ex+limb.side*reach/4,limb.ey-height/10,
            1,h+39,tone);
        for(int clump=0;clump<3;++clump)
            ht_leaf_cluster(b,limb.ex-limb.side*clump*reach/5,limb.ey+clump*3,
                ht_max(7,height/15),h+(unsigned)clump*41u,tone);
        ht_checkpoint();
    }
    /* Bark follows the trunk's growth instead of random screen-space noise.
     * Furrows stop/rejoin; knot rings surround scars rather than white holes. */
    if(width>=16 && tone>=220) {
        for(int seam=0;seam<5;++seam) {
            int previous_x=0,previous_y=0;
            for(int n=2;n<51;++n) {
                int c,l,r;ht_organic_trunk_node(&t,n,&c,&l,&r);
                int sx=c-l/2+(l+r)*seam/10,sy=base-height*n/64;
                if(n>2 && (n+seam*7)%17>3 && ht_organic_visible(b,ht_min(previous_x,sx)-1,sy,ht_max(previous_x,sx)+1,previous_y))
                    ht_limb(b,previous_x,previous_y,sx,sy,0,0,ht_max(0,tone-24-seam%2*12));
                previous_x=sx;previous_y=sy;
            }
        }
        for(int knot=0;knot<2;++knot) {
            int node=knot?42:23,c,l,r;ht_organic_trunk_node(&t,node,&c,&l,&r);
            int ky=base-height*node/64,kx=c+(knot?-l/3:r/3),w=ht_max(2,width/5);
            static const int8_t ring[13][2]={{0,-9},{3,-7},{5,-3},{6,1},{4,6},{1,9},{-2,8},{-5,4},{-6,0},{-4,-5},{-2,-8},{0,-9},{0,-9}};
            for(int n=1;n<12;++n)ht_limb(b,kx+ring[n-1][0]*w/6,ky+ring[n-1][1]*w/4,
                kx+ring[n][0]*w/6,ky+ring[n][1]*w/4,0,0,ht_max(0,tone-43));
            ht_limb(b,kx,ky-w,kx+1,ky+w,1,0,ht_max(0,tone-17));
        }
    }
}

/* Short dead wood has torn fibres and a broad broken end, not a conical tip. */
static void ht_organic_snag(uint8_t *b,int x,int base,int height,int width,unsigned seed,int tone) {
    int px=x,py=base,pr=width+2;
    for(int n=1;n<=16;++n) {
        unsigned h=ht_hash(seed+(unsigned)n*47u);
        int nx=x-n/6+(int)(h%3)-1,ny=base-height*n/16;
        int r=ht_max(2,width*(28-n)/28)+(int)((h>>4)%2);
        ht_triangle(b,px-pr,py,px+pr,py,nx-r,ny,tone);
        ht_triangle(b,px+pr,py,nx+r,ny,nx-r,ny,tone);
        if(n>2 && n%5)ht_limb(b,px-2,py,nx-2,ny,0,0,tone-33);
        px=nx;py=ny;pr=r;
    }
    ht_triangle(b,px-pr,py+1,px-pr+1,py-4,px,py+1,tone);
    ht_triangle(b,px,py+2,px+pr-1,py-2,px+pr,py+1,tone);
}
