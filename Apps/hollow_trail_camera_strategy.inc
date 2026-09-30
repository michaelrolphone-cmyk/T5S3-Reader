/* The motion test reduces final scene sampling to 2x2 blocks outside a
 * 144x112 logical-pixel region centered on the character. Source rasterization
 * remains unchanged; this saves camera samples, not geometry construction.
 * UI is drawn later at full resolution. No frame-sized allocation is needed.
 * Visible-border clipping and the unchanged corner-safe camera coefficients
 * bound every source footprint, including the center of a coarse block. */
#define HT_DETAIL_HALF_W 72
#define HT_DETAIL_HALF_H 56
static inline __attribute__((always_inline)) uint8_t ht_camera_strategy_sample(const uint8_t *source,int u,int v,bool nearest) {
    if(nearest) {
        int x=(u+2048)>>12,y=(v+2048)>>12;
        return source[y*HT_W+x];
    }
    int ix=u>>12,iy=v>>12;
    int fx=(u>>8)&15,fy=(v>>8)&15;
    const uint8_t *a=source+iy*HT_W+ix,*b=a+HT_W;
    int top=a[0]*16+((int)a[1]-a[0])*fx,bottom=b[0]*16+((int)b[1]-b[0])*fx;
    return (uint8_t)((top*16+(bottom-top)*fy)>>8);
}
static __attribute__((optimize("O3"))) void ht_camera_strategy_into(const uint8_t *source,const ht_game *g,int cx,int cy,bool coarse,bool nearest) {
    int turn=0,scale=4096;
    if(g->sway_phase || g->drop_zoom)ht_camera_coefficients(g,&turn,&scale);
    if(!coarse) {
        for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y) {
            int left=ht_visible_left[y],right=HT_W-left;
            int u=(HT_W/2)*4096+(left-HT_W/2)*scale+(y-HT_H/2)*turn;
            int v=(HT_H/2)*4096-(left-HT_W/2)*turn+(y-HT_H/2)*scale;
            for(int x=left;x<right;++x,u+=scale,v-=turn) {
                ht_scene[y*HT_W+x]=ht_camera_strategy_sample(source,u,v,nearest);
            }
            ht_camera_full_samples+=(unsigned)(right-left);
            if((y&15)==15)ht_checkpoint();
        }
        return;
    }
    for(int y=0;y<HT_H;y+=2) {
        int u=(HT_W/2)*4096-(HT_W/2)*scale+(y-HT_H/2)*turn;
        int v=(HT_H/2)*4096+(HT_W/2)*turn+(y-HT_H/2)*scale;
        for(int x=0;x<HT_W;x+=2,u+=2*scale,v-=2*turn) {
            /* Entire 2x2 footprint must be outside the protected region.
             * Bounds include both affine axes and an interpolation guard. */
            int min_u=u+ht_min(0,turn),max_u=u+scale+ht_max(0,turn);
            int min_v=v+ht_min(0,-turn),max_v=v+scale+ht_max(0,-turn);
            bool outside=max_u<(cx-HT_DETAIL_HALF_W-2)*4096 || min_u>(cx+HT_DETAIL_HALF_W+2)*4096 ||
                         max_v<(cy-HT_DETAIL_HALF_H-2)*4096 || min_v>(cy+HT_DETAIL_HALF_H+2)*4096;
            bool interior=y>=HT_BORDER && y+1<HT_H-HT_BORDER &&
                x>=ht_visible_left[y] && x+1<HT_W-ht_visible_left[y] &&
                x>=ht_visible_left[y+1] && x+1<HT_W-ht_visible_left[y+1];
            if(outside && interior) {
                uint8_t ink=ht_camera_strategy_sample(source,u+(scale+turn)/2,v+(scale-turn)/2,nearest);
                ht_scene[y*HT_W+x]=ht_scene[y*HT_W+x+1]=ink;
                ht_scene[(y+1)*HT_W+x]=ht_scene[(y+1)*HT_W+x+1]=ink;
                ++ht_camera_coarse_blocks;
            } else {
                for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
                    int xx=x+dx,yy=y+dy;
                    if(yy<HT_BORDER || yy>=HT_H-HT_BORDER || xx<ht_visible_left[yy] || xx>=HT_W-ht_visible_left[yy])continue;
                    ht_scene[yy*HT_W+xx]=ht_camera_strategy_sample(source,u+dx*scale+dy*turn,v-dx*turn+dy*scale,nearest);
                    ++ht_camera_full_samples;
                }
            }
        }
        if((y&14)==14)ht_checkpoint();
    }
}
