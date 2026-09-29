/* Only fully covered 16x16 source blocks are eligible. Reuse the collision
 * surface and actual drawn terrain bottom/cliff inset, never the broad platform
 * collision rectangle alone. Erode by a full block for reconstruction support.
 * Gaps, fine silhouettes, foliage and soft/blended effects are not occluders. */
#define HT_OCC_COLS ((HT_W+15)/16)
#define HT_OCC_ROWS ((HT_H+15)/16)
static uint8_t ht_occlusion_solid[HT_OCC_ROWS][HT_OCC_COLS];
static uint8_t ht_occlusion_safe[HT_OCC_ROWS][HT_OCC_COLS];
static bool ht_occlusion_active;
static unsigned ht_occlusion_blocks,ht_occlusion_composite_skips,ht_occlusion_neural_skips;
static bool ht_background_hidden(int x,int y) {
    return ht_occlusion_active && ht_occlusion_safe[y/16][x/16];
}
static void ht_occlusion_rect(int left,int top,int right,int bottom) {
    left=ht_max(0,ht_project_x(left));right=ht_min(HT_W,ht_project_x(right));
    top=ht_max(0,ht_project_y(top));bottom=ht_min(HT_H,ht_project_y(bottom));
    for(int y=(top+15)/16;y<bottom/16;++y)
        for(int x=(left+15)/16;x<right/16;++x)ht_occlusion_solid[y][x]=1;
}
static void ht_occlusion_build(const ht_game *game) {
    memset(ht_occlusion_solid,0,sizeof(ht_occlusion_solid));memset(ht_occlusion_safe,0,sizeof(ht_occlusion_safe));
    ht_occlusion_blocks=ht_occlusion_composite_skips=ht_occlusion_neural_skips=0;
    int saved=ht_world_scale;ht_world_scale=256-game->vista*5/16;
    int camera=game->camera/256,cy=game->camera_y/256;
    int margin=(HT_H*256/ht_world_scale-HT_H)/2+2;
    for(int i=0;i<HT_PLATFORMS;++i) {
        for(int part=0;part<3;++part) {
            int l,r,flat;if(!ht_platform_piece(game,i,part,&l,&r,&flat))break;
            int begin=ht_max(l,camera-ht_view_margin()),end=ht_min(r,camera+HT_W+ht_view_margin());
            if(begin>=end)continue;
            int top=-520,bottom=520;
            for(int x=begin;x<end;++x) {
                top=ht_max(top,part==2?flat:ht_surface_at(game,i,x));
                bottom=ht_min(bottom,ht_terrain_bottom(game,i,x));
            }
            int left=begin,right=end;
            bool cliff=part==0 && bottom==520 &&
                ((ht_cliff_edge(game,i,-1) && l>=begin && l<end) ||
                 (ht_cliff_edge(game,i,1) && r>begin && r<=end));
            if(cliff) {
                bool a=ht_cliff_edge(game,i,-1),b=ht_cliff_edge(game,i,1);
                for(int y=ht_max(top,cy-margin);y<ht_min(bottom,cy+HT_H+margin);++y) {
                    if(a)left=ht_max(left,l+ht_cliff_inset(game,i,-1,y));
                    if(b)right=ht_min(right,r-ht_cliff_inset(game,i,1,y));
                }
            }
            if(right>left && bottom>top)ht_occlusion_rect(left-camera,top-cy,right-camera,bottom-cy);
        }
        ht_checkpoint();
    }
    ht_world_scale=saved;
    for(int y=1;y<HT_OCC_ROWS-1;++y)for(int x=1;x<HT_OCC_COLS-1;++x) {
        bool solid=true;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)solid=solid && ht_occlusion_solid[y+dy][x+dx];
        ht_occlusion_safe[y][x]=(uint8_t)solid;ht_occlusion_blocks+=(unsigned)solid;
    }
    ht_occlusion_active=true;
}
