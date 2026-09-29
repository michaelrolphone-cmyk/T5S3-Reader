/* Small deterministic silhouette clusters. Shared by cached groves and rooted
 * world trees: no textures, frame noise, allocation or off-screen filter. */
static void ht_leaf_cluster(uint8_t *b,int x,int y,int size,unsigned seed,int tone) {
    for(int leaf=0;leaf<11;++leaf) {
        unsigned h=ht_hash(seed+(unsigned)leaf*59u);
        int dx=(int)(h%(unsigned)(size*2+1))-size;
        int dy=(int)((h>>9)%(unsigned)(size+1))-size/2;
        int r=1+(int)((h>>18)%3),lean=(h&128u)?1:-1;
        ht_triangle(b,x+dx-r,y+dy,x+dx+lean*r,y+dy-r-1,x+dx+r+1,y+dy+1,tone);
    }
}
static void ht_fern(uint8_t *b,int x,int y,int height,int tone,unsigned seed) {
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
