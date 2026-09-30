/* Cheap conservative rejection before generating detailed off-screen shapes. */
static bool ht_organic_visible(uint8_t *b,int left,int top,int right,int bottom) {
    if(b==ht_scene && ht_world_scale!=256) {
        left=ht_project_x(left);right=ht_project_x(right);
        top=ht_project_y(top);bottom=ht_project_y(bottom);
    }
    return right>=0 && left<HT_W && bottom>=0 && top<HT_H;
}
/* Small deterministic silhouette clusters. Shared by cached groves and rooted
 * world trees: no textures, frame noise, allocation or off-screen filter. */
static void ht_leaf_cluster(uint8_t *b,int x,int y,int size,unsigned seed,int tone) {
    if(ht_geometry_level==0 && !ht_organic_visible(b,x-size-8,y-size-8,x+size+8,y+size+8))return;
    for(int leaf=0;leaf<11;++leaf) {
        unsigned h=ht_hash(seed+(unsigned)leaf*59u);
        int dx=(int)(h%(unsigned)(size*2+1))-size;
        int dy=(int)((h>>9)%(unsigned)(size+1))-size/2;
        int r=1+(int)((h>>18)%3),lean=(h&128u)?1:-1;
        if(ht_geometry_level==0) {
            /* A pointed oval with uneven shoulders, rather than a triangle. */
            static const int8_t leaf_edge[8][2]={{0,-4},{2,-2},{3,0},{2,2},{0,3},{-2,2},{-3,0},{-2,-2}};
            for(int v=0;v<8;++v) {
                int n=(v+1)%8;
                ht_triangle(b,x+dx,y+dy,x+dx+leaf_edge[v][0]*r/3+lean*leaf_edge[v][1]/3,
                    y+dy+leaf_edge[v][1]*r/3,x+dx+leaf_edge[n][0]*r/3+lean*leaf_edge[n][1]/3,
                    y+dy+leaf_edge[n][1]*r/3,tone);
            }
            continue;
        }
        ht_triangle(b,x+dx-r,y+dy,x+dx+lean*r,y+dy-r-1,x+dx+r+1,y+dy+1,tone);
    }
}
static void ht_fern(uint8_t *b,int x,int y,int height,int tone,unsigned seed) {
    if(ht_geometry_level==0) {
        /* Arching rachises carry progressively smaller paired pinnae. */
        for(int frond=0;frond<3;++frond) {
            int side=frond==0?-1:1,span=height*(frond==2?1:3)/4;
            int px=x,py=y;
            for(int k=1;k<=8;++k) {
                int nx=x+side*span*k*k/64,ny=y-height*k/8+height*k*k/112;
                ht_limb(b,px,py,nx,ny,0,0,tone);
                int w=(9-k)*height/24;
                for(int pin=-1;pin<=1;pin+=2) {
                    int ex=nx+pin*w,ey=ny-height/6;
                    int mx=(nx+ex)/2,my=(ny+ey)/2;
                    ht_triangle(b,nx,ny,mx-1,my,ex,ey,tone);
                    ht_triangle(b,nx,ny,ex,ey,mx+1,my+1,tone);
                }
                px=nx;py=ny;
            }
        }
        return;
    }
    for(int side=-1;side<=1;side+=2) {
        int tip=x+side*(height/2+(int)(seed%5)),ty=y-height;
        ht_limb(b,x,y,tip,ty,1,0,tone);
        for(int k=1;k<5;++k) {
            int at=x+(tip-x)*k/5,ay=y-height*k/5,w=(6-k)*height/22+1;
            ht_limb(b,at,ay,at-side*w,ay-height/8,1,0,tone);
            ht_limb(b,at,ay,at+side*w,ay+1,1,0,tone);
        }
    }
}
