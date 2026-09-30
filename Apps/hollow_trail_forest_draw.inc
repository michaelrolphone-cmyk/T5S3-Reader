/* Chapter-scale composition. These are continuous depth planes, not tiles of
 * small props. Lighting is evaluated before actors/UI and follows the world. */
typedef struct {int16_t light_x,light_y;int16_t ridge[9];} ht_composition;
static const ht_composition ht_forest_composition={
    370,-80,{140,72,116,40,93,18,85,38,125}
};
static int ht_composed_ridge(const ht_composition *c,int x,int plane) {
    int q=ht_floor_div(x+600,340),u=x+600-q*340;
    int n=((q%8)+8)%8;
    if(plane!=2)u=u*u/340*(1020-2*u)/340;
    return c->ridge[n]+(c->ridge[n+1]-c->ridge[n])*u/340+plane*34;
}
static void ht_forest_backdrop_low(const ht_game *g) {
    if(g->level!=0)return;
    const ht_composition *c=&ht_forest_composition;
    int cx=g->camera/256,cy=g->camera_y/256;
    uint8_t *cached=ht_cached_near[2][0];
    if(ht_forest_frame_valid && ht_composed_frame_level==0 && ht_forest_frame_x==cx && ht_forest_frame_y==cy &&
       ht_forest_frame_scale==ht_world_scale) {
        memcpy(ht_scene,cached,HT_PIXELS);return;
    }
    ht_forest_frame_valid=false;
    /* Three distant openings carry the composition across the full chapter. */
    int lx=ht_project_x(c->light_x-cx*26/100),ly=ht_project_y(c->light_y-cy*26/100);
    static int16_t ridges[3][HT_W]; /* Keep large raster scratch off the app stack. */
    for(int p=0;p<3;++p)for(int x=0;x<HT_W;++x) {
        int parallax=18+p*19;
        int wx=240+(x-240)*256/ht_world_scale+cx*parallax/100;
        ridges[p][x]=ht_project_y(ht_composed_ridge(c,wx,p)-cy*parallax/100);
    }
    /* Distant tones are soft by design. Evaluate at the existing scenery
     * sampling scale and expand contiguous rows; foreground stays full res. */
    int opening=286*ht_world_scale/256;
    static int dx2[HT_W/2];
    for(int x=0;x<HT_W;x+=2) {
        int dx=ht_min(ht_abs(x-lx),ht_min(ht_abs(x-lx-opening),ht_abs(x-lx-2*opening)));
        dx2[x/2]=dx*dx;
    }
    for(int y=0;y<HT_H;y+=2) {
        int dy=y-ly,dy2=dy*dy/2;
        uint8_t *row=ht_scene+y*HT_W;
        for(int x=0;x<HT_W;x+=2) {
            int radial=ht_min(180,(dx2[x/2]+dy2)/750);
            int ink=ht_clamp(22+radial,10,155);
            /* Each nearer ridge is darker than the entire previous range;
             * stop at the nearest hit instead of shading hidden planes. */
            for(int p=2;p>=0;--p)if(y>=ridges[p][x]) {
                ink=ht_max(ink,58+p*43+ht_min(32,(y-ridges[p][x])/5));break;
            }
            row[x]=row[x+1]=(uint8_t)ink;
        }
        memcpy(row+HT_W,row,HT_W);
        if((y&15)==14)ht_checkpoint();
    }
    /* Sparse, monumental mid-distance silhouettes rooted into their own hills. */
    {
        for(int p=0;p<2;++p) {
            int parallax=37+p*19,spacing=225-p*42;
            int offset=cx*parallax/100;
            int begin=ht_floor_div(offset-ht_view_margin()-180,spacing);
            int end=ht_floor_div(offset+HT_W+ht_view_margin()+180,spacing);
            for(int n=begin;n<=end;++n) {
                unsigned seed=ht_hash((unsigned)n+91u+p*719u);
                int wx=n*spacing+(int)(seed%63),base=ht_composed_ridge(c,wx,p+1);
                ht_tree(ht_scene,wx-offset,base+3-cy*parallax/100,270+(int)(seed%180),11+p*5,seed,95+p*57);
            }
            ht_checkpoint();
        }
    }
    /* Only the two narrow shaft spans need illumination work. */
    for(int y=0;y<HT_H;++y) {
        int beam=lx+(y-ly)/3,fade=ht_clamp(y+40,0,160);
        uint8_t *row=ht_scene+y*HT_W;
        for(int shaft=0;shaft<2;++shaft) {
            int center=beam+shaft*89;
            int left=ht_max(0,center-29),right=ht_min(HT_W,center+30);
            for(int x=left;x<right;++x)if(row[x]<195) {
                int strength=(30-ht_abs(x-center))*fade/160;
                row[x]=(uint8_t)ht_max(6,row[x]-strength);
            }
        }
        if((y&15)==15)ht_checkpoint();
    }

    if(!ht_abort) {
        memcpy(cached,ht_scene,HT_PIXELS);
        ht_forest_frame_x=cx;ht_forest_frame_y=cy;ht_forest_frame_scale=ht_world_scale;
        ht_composed_frame_level=0;ht_forest_frame_valid=true;
    }

}
static void ht_forest_backdrop(const ht_game *g) {
    if(g->level!=0)return;
    if(!ht_native_active){ht_forest_backdrop_low(g);return;}
    uint8_t *native=ht_scene;
    ht_scene=ht_scene_low;ht_native_active=false;
    ht_forest_backdrop_low(g);
    ht_scene=native;ht_native_active=true;
    ht_expand_scene_native(ht_scene_low,native);
}
/* One dead snag at the washed-out bank. Its root hinge stays in the soil;
 * the crown accelerates through a circular fall and comes to rest on the far
 * bank. It has no switch, cables, moving ground or invisible early foothold. */
static void ht_forest_log_draw(const ht_game *g) {
    int cx=g->camera/256,cy=g->camera_y/256;
    int phase=g->traversal.forest_log_phase;
    int angle=ht_forest_log_angle(g);
    int sn=ht_forest_log_sine(angle),cs=ht_forest_log_sine(4096-angle);
    int dx=312*sn/256,dy=-312*cs/256;
    int x=374-cx,y=220-cy,ex=x+dx,ey=y+dy;
    if(ex<-ht_view_margin()-40 || x>HT_W+ht_view_margin()+40)return;
    /* The walkable upper edge keeps the established bend and taper. Bark
     * plates break the lower contour into 32 sections, rotating with the log. */
    int px=x,py=y,pr=12,pbottom=12;
    for(int i=1;i<=32;++i) {
        int nx,ny,radius,bottom;
        ht_forest_log_node(dx,dy,i,&nx,&ny,&radius,&bottom);nx-=cx;ny-=cy;
        int ptx=px+dy*pr/312,pty=py-dx*pr/312;
        int pbx=px-dy*pbottom/312,pby=py+dx*pbottom/312;
        int ntx=nx+dy*radius/312,nty=ny-dx*radius/312;
        int nbx=nx-dy*bottom/312,nby=ny+dx*bottom/312;
        ht_triangle(ht_scene,ptx,pty,pbx,pby,ntx,nty,250);
        ht_triangle(ht_scene,pbx,pby,nbx,nby,ntx,nty,250);
        for(int seam=-1;seam<=1;++seam)if((i+seam*3)%9!=0) {
            ht_limb(ht_scene,px-dy*seam/78,py+dx*seam/78,
                nx-dy*seam/78,ny+dx*seam/78,0,0,185+seam*14);
        }
        if(i==11 || i==23) {
            ht_limb(ht_scene,nx-dx/80,ny-dy/80,nx-dy/100,ny+dx/100,0,0,160);
            ht_limb(ht_scene,nx-dy/100,ny+dx/100,nx+dx/80,ny+dy/80,0,0,160);
        }
        px=nx;py=ny;pr=radius;pbottom=bottom;
    }
    /* Sparse broken limb stubs turn with the trunk. None become a ladder. */
    for(int i=1;i<4;++i) {
        int at=x+dx*i/5,ay=y+dy*i/5,side=i%2?1:-1;
        ht_limb(ht_scene,at,ay,at-dy*side/15,ay+dx*side/15,3,0,245);
    }
    ht_triangle(ht_scene,x-19,y+8,x-8,y-16,x+12,y+7,255);
    ht_limb(ht_scene,x-17,y+7,x-34,y+13,3,0,240);
    if(phase==32) {
        /* Ground contacts bury both ends. Narrow lit bark stays above them. */
        ht_limb(ht_scene,x-6,y+9,x+16,y+7,2,0,235);
        ht_limb(ht_scene,ex-12,ey+8,ex+11,ey+7,2,0,235);
    }
}
/* Paired contour vertices form non-overlapping strips. More vertices follow
 * broad organic bends rather than adding noise or extra scenery. Each strip
 * uses the clipped triangle rasterizer; no heap, full-frame scratch or change
 * to the distant cached planes. Coordinates stay in world units until draw. */
typedef struct {int16_t ax,ay,bx,by;} ht_foreground_section;
static void ht_foreground_contour(int x,int y,const ht_foreground_section *v,int count,int tone) {
    for(int n=1;n<count;++n) {
        const ht_foreground_section *a=&v[n-1],*b=&v[n];
        ht_triangle(ht_scene,x+a->ax,y+a->ay,x+a->bx,y+a->by,x+b->ax,y+b->ay,tone);
        ht_triangle(ht_scene,x+a->bx,y+a->by,x+b->bx,y+b->by,x+b->ax,y+b->ay,tone);
        if((n&7)==0)ht_checkpoint();
    }
}
/* The same two close trunks belong to the observer's bank. Their 130-vertex
 * contours, bark furrows and knots use the living-tree anatomy; the existing
 * broken branch and root collar retain their separate asymmetric outlines. */
static void ht_forest_foreground(const ht_game *g) {
    if(g->level!=0)return;
    static const int anchors[2]={1100,2800};
    static const ht_foreground_section branch[]={
        {-22,-236,-20,-277},{-42,-263,-36,-287},
        {-54,-281,-46,-297},{-65,-295,-58,-306},
        {-77,-307,-71,-316},{-89,-317,-85,-323},
        {-98,-324,-95,-329},{-104,-328,-101,-331}
    };
    static const ht_foreground_section root[]={
        {10,-31,8,14},{24,-17,24,17},{32,-8,34,19},
        {41,-1,44,21},{53,7,56,23},{66,13,70,24},
        {78,19,80,24},{88,24,88,25}
    };
    int offset=g->camera/256*5/4,cy=g->camera_y/256*5/4;
    for(int i=0;i<2;++i) {
        int x=anchors[i]-offset,y=365-cy;
        if(x< -105-ht_view_margin() || x>HT_W+105+ht_view_margin())continue;
        ht_foreground_contour(x,y,branch,sizeof(branch)/sizeof(branch[0]),244);
        ht_foreground_contour(x,y,root,sizeof(root)/sizeof(root[0]),244);
        ht_organic_tree(ht_scene,x-10,y,700,30,3217u+(unsigned)i*43u,244,false);
        /* A restrained seam follows the bend instead of a straight slash. */
        ht_limb(ht_scene,x-28,y-265,x-23,y-292,1,1,180);
        ht_limb(ht_scene,x-23,y-292,x-16,y-322,1,0,180);
    }
}
