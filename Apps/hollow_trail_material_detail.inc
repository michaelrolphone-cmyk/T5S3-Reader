/* Photo-guided material outlines. Existing footprints and object counts stay
 * fixed; detail uses short bounded strips, never a per-pixel noise texture. */
static void ht_detail_rock(int x,int y,int w,int h,unsigned seed,int kind) {
    static const int8_t rim[36][2]={
        {-100,3},{-99,-12},{-94,-15},{-94,-33},{-88,-39},{-84,-54},
        {-74,-58},{-72,-73},{-58,-76},{-57,-91},{-39,-92},{-31,-98},
        {-13,-97},{-6,-100},{13,-98},{19,-89},{35,-88},{41,-78},
        {57,-77},{61,-65},{70,-62},{74,-48},{82,-46},{85,-34},
        {92,-30},{94,-18},{98,-16},{100,1},{87,4},{69,6},
        {48,8},{22,7},{0,9},{-25,7},{-49,8},{-74,5}
    };
    int flip=seed&1?1:-1;
    for(int i=0;i<36;++i) {
        int j=(i+1)%36;
        int ax=x+flip*rim[i][0]*w/100,ay=y+rim[i][1]*h/100;
        int bx=x+flip*rim[j][0]*w/100,by=y+rim[j][1]*h/100;
        ht_triangle(ht_scene,x-w/9,y-h/3,ax,ay,bx,by,i<14?187:i<27?212:199);
        if(kind==HT_SNOW && i>=7 && i<20)
            ht_limb(ht_scene,ax,ay,bx,by,2,1,108);
    }
    /* Two connected fracture planes, with chips at their intersections. */
    int fx=x-w/5,fy=y-h*4/5;
    ht_limb(ht_scene,fx,fy,fx+w/8,fy+h/3,0,0,145);
    ht_limb(ht_scene,fx+w/8,fy+h/3,x+w/3,y-h/5,0,0,151);
    ht_limb(ht_scene,fx+w/8,fy+h/3,x-w/2,y-h/4,0,0,170);
}
static void ht_detail_cactus(int x,int y,int h,unsigned seed) {
    int radius=ht_max(3,h/14),px=x,py=y;
    for(int k=1;k<=24;++k) {
        int nx=x+(int)(seed%3)*k/24,ny=y-(h-radius)*k/24;
        ht_limb(ht_scene,px,py,nx,ny,radius,radius,255);
        px=nx;py=ny;
    }
    ht_disk(ht_scene,px,py,radius,255);
    for(int arm=0;arm<2;++arm) {
        int side=arm?1:-1,root=y-h*(arm?43:58)/100;
        int reach=h*(arm?23:28)/100,tip=root-h*(arm?28:22)/100;
        int ax=x,ay=root,previous_radius=radius-1;
        for(int k=1;k<=12;++k) {
            int v=12-k;
            int nx=x+side*reach*(1728-v*v*v)/1728;
            int ny=root+(3*v*v*k*6+3*v*k*k*(tip-root+12)+k*k*k*(tip-root))/1728;
            int arm_radius=ht_max(1,radius-1-k/8);
            ht_limb(ht_scene,ax,ay,nx,ny,previous_radius,arm_radius,255);
            ht_disk(ht_scene,nx,ny,arm_radius,255);previous_radius=arm_radius;
            if(k>2)ht_limb(ht_scene,ax-side,ay,nx-side,ny,0,0,201);
            ax=nx;ay=ny;
        }
        ht_disk(ht_scene,ax,ay,ht_max(1,radius-2),255);

    }
    for(int rib=-1;rib<=1;++rib)ht_limb(ht_scene,x+rib*2,y-5,x+rib*2+(int)(seed%3),y-h+6,0,0,205);
    ht_disk(ht_scene,x+1,y-h*3/5,2,165);
}
static void ht_detail_tree_bark(const ht_game *g,int x,int base,int height,int width,unsigned seed) {
    if(g->level!=6 && g->level!=8)return;
    unsigned hash=ht_hash(seed);int lean=(int)(hash%43)-21,bend=(int)((hash>>8)%19)-9;
    int kx=x+lean/3+bend,ky=base-height*52/100,tx=x+lean;
    int px=x,py=base,pr=width;
    for(int k=1;k<=40;++k) {
        int ny=base-height*k/40,nx,r;
        if(ny>=ky){int u=(base-ny)*256/ht_max(1,base-ky);nx=x+(kx-x)*u/256;r=width+(ht_max(2,width*2/3)-width)*u/256;}
        else {int u=(ky-ny)*256/ht_max(1,ky-base+height);nx=kx+(tx-kx)*u/256;r=ht_max(2,width*2/3)+(1-ht_max(2,width*2/3))*u/256;}
        int bulge=k%11==4?1:0;
        ht_limb(ht_scene,px,py,nx,ny,pr,r+bulge,255);
        if(k<35)ht_limb(ht_scene,px+pr/3,py,nx+r/3,ny,0,0,208);
        if(k%13==6){ht_disk(ht_scene,nx-r/3,ny,2,187);ht_disk(ht_scene,nx-r/3,ny,1,245);}
        px=nx;py=ny;pr=r+bulge;
    }
    /* Existing branch axes/footholds stay exact. Fine outer twigs and sparse
     * needle fans break the alpine outline without creating new footholds. */
    for(int j=0;j<5;++j) {
        ht_tree_branch b=ht_tree_branch_shape(x,base,height,width,seed,j);
        for(int k=0;k<5;++k) {
            int bx=b.mx+(b.ex-b.mx)*k/5,by=b.my+(b.ey-b.my)*k/5;
            int ex=bx+b.side*(6+k),ey=by-5-(k%3)*3;
            ht_limb(ht_scene,bx,by,ex,ey,1,0,250);
            if(g->level==8)for(int needle=0;needle<3;++needle)
                ht_limb(ht_scene,ex-needle*b.side,ey+2,ex+b.side*(3+needle),ey-3-needle,0,0,241);
        }
    }
}
static void ht_detail_leaf(int x,int y,int ex,int ey,int width,int tone) {
    int dx=ex-x,dy=ey-y,len=ht_max(1,(int)ht_isqrt((unsigned)(dx*dx+dy*dy)));
    int px=x,py=y,pr=0;
    for(int k=1;k<=12;++k) {
        int nx=x+dx*k/12,ny=y+dy*k/12+(k*(12-k))/24;
        int r=width*k*(12-k)/36;
        int ax=-dy*pr/len,ay=dx*pr/len,bx=-dy*r/len,by=dx*r/len;
        ht_triangle(ht_scene,px+ax,py+ay,px-ax,py-ay,nx+bx,ny+by,tone);
        ht_triangle(ht_scene,px-ax,py-ay,nx-bx,ny-by,nx+bx,ny+by,tone);
        px=nx;py=ny;pr=r;
    }
    ht_limb(ht_scene,x,y,ex,ey,0,0,tone-25);
}
static void ht_detail_close_rock(int x,int y,bool snow) {
    int pl=x-39,pr=x+15,py=y-460;
    for(int k=1;k<=48;++k) {
        unsigned h=ht_hash((unsigned)(k*47+19));
        int ny=y-460+k*21,l=x-35-(int)(h%12)-k/4,r=x+23+(int)((h>>9)%15)+k/3;
        ht_triangle(ht_scene,pl,py,pr,py,l,ny,248);
        ht_triangle(ht_scene,pr,py,r,ny,l,ny,248);
        if(k%5==0){ht_limb(ht_scene,r-3,ny-15,r-17,ny-5,0,0,snow?136:207);ht_limb(ht_scene,r-17,ny-5,r-11,ny+12,0,0,216);}
        pl=l;pr=r;py=ny;
    }
}
