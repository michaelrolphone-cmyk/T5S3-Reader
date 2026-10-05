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
static void ht_forest_backdrop_native(const ht_game *g) {
    const ht_composition *c=&ht_forest_composition;
    int cx=g->camera/256,cy=g->camera_y/256;
    int lx=ht_project_x(c->light_x-cx*26/100)*HT_NATIVE_SCALE;
    int ly=ht_project_y(c->light_y-cy*26/100)*HT_NATIVE_SCALE;
    static int16_t ridges[3][HT_NATIVE_W];
    for(int p=0;p<3;++p)for(int sx=0;sx<HT_NATIVE_W;++sx) {
        int x=sx/HT_NATIVE_SCALE,parallax=18+p*19;
        int wx=240+(x-240)*256/ht_world_scale+cx*parallax/100;
        ridges[p][sx]=(int16_t)(ht_project_y(ht_composed_ridge(c,wx,p)-cy*parallax/100)*HT_NATIVE_SCALE);
    }
    int opening=286*ht_world_scale/256*HT_NATIVE_SCALE;
    for(int sy=0;sy<HT_NATIVE_H;++sy) {
        int dy=(sy-ly)/HT_NATIVE_SCALE,dy2=dy*dy/2;
        uint8_t *row=ht_scene+sy*HT_NATIVE_W;
        for(int sx=0;sx<HT_NATIVE_W;++sx) {
            int dx=ht_min(ht_abs(sx-lx),ht_min(ht_abs(sx-lx-opening),ht_abs(sx-lx-2*opening)))/HT_NATIVE_SCALE;
            int radial=ht_min(180,(dx*dx+dy2)/750);
            int ink=ht_clamp(22+radial,10,155);
            for(int p=2;p>=0;--p)if(sy>=ridges[p][sx]) {
                int depth=(sy-ridges[p][sx])/HT_NATIVE_SCALE;
                ink=ht_max(ink,58+p*43+ht_min(32,depth/5));break;
            }
            row[sx]=(uint8_t)ink;
        }
        if((sy&31)==31)ht_checkpoint();
    }
    /* Geometry now reaches the physical raster directly: its sloped edges and
     * branch contours can occupy odd panel pixels instead of 2x2 blocks. */
    for(int p=0;p<2;++p) {
        int parallax=37+p*19,spacing=225-p*42,offset=cx*parallax/100;
        int begin=ht_floor_div(offset-ht_view_margin()-180,spacing);
        int end=ht_floor_div(offset+HT_W+ht_view_margin()+180,spacing);
        for(int n=begin;n<=end;++n) {
            unsigned seed=ht_hash((unsigned)n+91u+p*719u);
            int wx=n*spacing+(int)(seed%63),base=ht_composed_ridge(c,wx,p+1);
            ht_tree(ht_scene,wx-offset,base+3-cy*parallax/100,270+(int)(seed%180),11+p*5,seed,95+p*57);
        }
        ht_checkpoint();
    }
    for(int sy=0;sy<HT_NATIVE_H;++sy) {
        int beam=lx+(sy-ly)/3,fade=ht_clamp(sy/HT_NATIVE_SCALE+40,0,160);
        uint8_t *row=ht_scene+sy*HT_NATIVE_W;
        for(int shaft=0;shaft<2;++shaft) {
            int center=beam+shaft*89*HT_NATIVE_SCALE;
            int left=ht_max(0,center-29*HT_NATIVE_SCALE),right=ht_min(HT_NATIVE_W,center+30*HT_NATIVE_SCALE);
            for(int sx=left;sx<right;++sx)if(row[sx]<195) {
                int strength=(30*HT_NATIVE_SCALE-ht_abs(sx-center))*fade/(160*HT_NATIVE_SCALE);
                row[sx]=(uint8_t)ht_max(6,row[sx]-strength);
            }
        }
        if((sy&31)==31)ht_checkpoint();
    }
}
static void ht_forest_backdrop(const ht_game *g) {
    if(g->level!=0)return;
    if(ht_native_active){ht_forest_backdrop_native(g);return;}
    ht_forest_backdrop_low(g);
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

/* Chapter I's register belongs to a mill in the existing low hollow, not a
 * document on an unrelated stump. The open/broken downhill wall exposes the
 * desk while the player retains the established continuous ground route. */
static void ht_forest_mill(const ht_game *g) {
    if(g->level!=0)return;
    int cx=g->camera/256,cy=g->camera_y/256,x=1095-cx,base=218-cy;
    if(x+208< -ht_view_margin() || x-70>HT_W+ht_view_margin())return;
    /* Stream-side stone footing and a leaf-choked pool behind the route. */
    ht_triangle(ht_scene,x-55,base+2,x+171,base+2,x+140,base-17,74);
    ht_triangle(ht_scene,x-55,base+2,x+140,base-17,x-34,base-10,74);
    for(int i=0;i<15;++i) {
        int xx=x-40+(i*29)%166,yy=base-3-(i%3)*3;
        ht_limb(ht_scene,xx,yy,xx+9,yy,0,0,132+(i%3)*14);
    }
    /* Uphill stone wall, leaning timber front and a visibly sagging roof. */
    ht_triangle(ht_scene,x+117,base-92,x+173,base-68,x+168,base,193);
    ht_triangle(ht_scene,x+117,base-92,x+168,base,x+109,base,193);
    for(int row=0;row<6;++row)for(int col=0;col<3;++col) {
        int xx=x+119+col*17+(row&1)*3,yy=base-8-row*11;
        ht_limb(ht_scene,xx,yy,xx+12,yy-1,0,0,163);
    }
    ht_rect(ht_scene,x,base-86,122,86,170);
    for(int plank=0;plank<11;++plank) {
        int xx=x+plank*11;ht_limb(ht_scene,xx,base-84,xx+2,base-3,0,0,203);
        ht_limb(ht_scene,xx+3,base-48,xx+4,base-9,0,0,189);
    }
    ht_triangle(ht_scene,x-11,base-87,x+55,base-119,x+118,base-102,210);
    ht_triangle(ht_scene,x-11,base-87,x+118,base-102,x+182,base-75,210);
    /* Uneven courses sag over the wheel; missing ends expose the dark roof
     * beneath. Large planes remain legible, wear never becomes frame noise. */
    for(int row=0;row<4;++row)for(int tile=0;tile<7;++tile) {
        int xx=x+7+row*9+tile*18,yy=base-92-row*4+tile*2;
        if((tile+row*2)%9==5)continue;
        ht_limb(ht_scene,xx,yy,xx+14+(tile%3),yy+1,0,0,170);
        ht_limb(ht_scene,xx+14,yy+1,xx+14,yy+4,0,0,186);
    }
    int px=x-9,py=base-85;
    for(int i=1;i<=8;++i) {
        int nx=x-9+191*i/8,ny=base-85+10*i/8+5*i*(8-i)/16;
        ht_limb(ht_scene,px,py,nx,ny,2,2,244);px=nx;py=ny;
    }
    /* Water-blackened board feet and two splits follow real plank seams. */
    for(int i=0;i<10;++i) {
        int xx=x+3+i*11,height=11+(i*7)%19;
        ht_limb(ht_scene,xx,base-2,xx+1,base-height,2,0,224);
    }
    ht_limb(ht_scene,x+22,base-48,x+25,base-31,0,0,230);
    ht_limb(ht_scene,x+25,base-31,x+23,base-21,0,0,230);
    /* Three saplings in the gutter are roots in debris, not decorative trees. */
    for(int i=0;i<3;++i) {
        int xx=x+70+i*20,yy=base-83+i*2;
        ht_limb(ht_scene,xx,yy,xx-3,yy-18-i*4,1,0,153);
        ht_limb(ht_scene,xx-2,yy-11,xx+7,yy-19,1,0,148);
        ht_limb(ht_scene,xx-2,yy-10,xx-10,yy-17,1,0,148);
    }
    /* Still wheel: asymmetric waterline and spokes. No frame-based rotation. */
    int wx=x-18,wy=base-24;
    ht_disk(ht_scene,wx,wy,29,232);ht_disk(ht_scene,wx,wy,24,83);
    static const int8_t spokes[8][2]={{24,0},{17,17},{0,24},{-17,17},{-24,0},{-17,-17},{0,-24},{17,-17}};
    for(int i=0;i<8;++i)ht_limb(ht_scene,wx,wy,wx+spokes[i][0],wy+spokes[i][1],2,1,208);
    ht_disk(ht_scene,wx,wy,5,244);
    for(int i=0;i<7;++i)ht_limb(ht_scene,wx-25+i*8,base-7,wx-22+i*8,base-5,2,1,151);
    for(int i=0;i<4;++i)ht_limb(ht_scene,wx-23+i*12,base-1,wx-22+i*12,base+7,0,0,39);
    /* Broken shutter / missing wall make a quiet legible interior. */
    ht_rect(ht_scene,x+47,base-72,66,64,225);
    ht_triangle(ht_scene,x+52,base-72,x+91,base-72,x+111,base-8,103);
    ht_triangle(ht_scene,x+52,base-72,x+111,base-8,x+66,base-8,103);
    ht_limb(ht_scene,x+66,base-72,x+80,base-8,1,1,161);
    ht_limb(ht_scene,x+53,base-51,x+97,base-51,1,1,158);
    ht_triangle(ht_scene,x+42,base-65,x+29,base-56,x+29,base-17,202);
    ht_triangle(ht_scene,x+42,base-65,x+29,base-17,x+42,base-29,202);
    for(int i=0;i<3;++i)ht_limb(ht_scene,x+32,base-50+i*10,x+42,base-56+i*10,0,0,164);
    /* Novella I: the door under the eaves is barred from within; the broken
     * shutter remains the open entry beside it. The cutaway shows the timber
     * across the inner face, seated in two brackets, behind the stone jambs. */
    ht_triangle(ht_scene,x+127,base-57,x+158,base-48,x+155,base-2,235);
    ht_triangle(ht_scene,x+127,base-57,x+155,base-2,x+126,base-2,235);
    for(int board=0;board<3;++board)
        ht_limb(ht_scene,x+132+board*8,base-48+board*2,x+131+board*8,base-4,0,0,204);
    ht_limb(ht_scene,x+126,base-40,x+158,base-36,2,2,135);
    ht_rect(ht_scene,x+126,base-43,4,8,249);
    ht_rect(ht_scene,x+154,base-39,4,8,249);
    ht_limb(ht_scene,x+125,base-58,x+159,base-49,2,2,244);
    ht_limb(ht_scene,x+125,base-58,x+124,base-2,2,2,226);
    ht_limb(ht_scene,x+160,base-49,x+158,base-2,2,2,226);
    /* Sparse fern/ivy contact remains grounded in the uphill wall. */
    for(int i=0;i<5;++i) {
        int xx=x+138+i*7,yy=base-i*10;
        ht_limb(ht_scene,xx,yy,xx-8,yy-12,1,0,136);
        ht_disk(ht_scene,xx-7,yy-9,3,123);
        if(i)ht_limb(ht_scene,xx-7,yy-9,xx-14,yy+2,1,0,138);
    }
    /* Fern fronds root at the damp uphill wall, with tapering paired pinnae. */
    for(int frond=0;frond<4;++frond) {
        int root=x+155+frond*7,side=frond&1?1:-1;
        for(int k=0;k<5;++k) {
            int xx=root+side*k*3,yy=base-3-k*5+k*k/3,w=6-k;
            ht_limb(ht_scene,xx,yy,xx+side*3,yy-4,0,0,149);
            ht_limb(ht_scene,xx,yy,xx-w,yy-4,1,0,137);
            ht_limb(ht_scene,xx,yy,xx+w,yy-3,1,0,137);
        }
    }
    ht_checkpoint();
}
