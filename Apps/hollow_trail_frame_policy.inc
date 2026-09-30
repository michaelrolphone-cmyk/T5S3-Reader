/* Optional, independently selectable frame strategies over the proven renderer.
 * The retained raster contains scenery only, never actors, UI or packed dots. */
enum {HT_FRAME_BASE,HT_FRAME_ALTERNATE,HT_FRAME_NEAREST,HT_FRAME_FOVEATED,HT_FRAME_ALL,HT_FRAME_COUNT};
enum {HT_FRAME_ALT_BIT=1,HT_FRAME_NEAREST_BIT=2,HT_FRAME_FOVEATED_BIT=4};
static unsigned ht_frame_mode;
static uint8_t *ht_retained_background;
static bool ht_background_valid,ht_background_due=true,ht_motion_valid;
static bool ht_frame_reused,ht_frame_low_active;
static ht_game ht_background_game,ht_motion_game;
static unsigned ht_background_builds,ht_background_reuses,ht_camera_full_samples,ht_camera_coarse_blocks;
static unsigned ht_frame_flags(void) {
    static const uint8_t flags[HT_FRAME_COUNT]={0,1,2,4,7};
    return flags[ht_frame_mode<HT_FRAME_COUNT?ht_frame_mode:0];
}
static const char *ht_frame_name(void) {
    static const char *const names[HT_FRAME_COUNT]={"BASELINE","ALTERNATE BACKGROUND","NEAREST CAMERA","MOTION LOW + CHARACTER DETAIL","ALL THREE"};
    return names[ht_frame_mode<HT_FRAME_COUNT?ht_frame_mode:0];
}
static void ht_frame_invalidate(void) {
    ht_background_valid=false;ht_background_due=true;ht_motion_valid=false;
    ht_frame_reused=ht_frame_low_active=false;
}
static void ht_frame_select(unsigned mode) {
    ht_frame_mode=mode%HT_FRAME_COUNT;ht_frame_invalidate();
    ht_background_builds=ht_background_reuses=ht_camera_full_samples=ht_camera_coarse_blocks=0;
}
static bool ht_frame_moving(const ht_game *g) {
    bool moving=g->vx || g->vy;
    if(ht_motion_valid) moving=moving || g->x!=ht_motion_game.x || g->y!=ht_motion_game.y ||
        g->camera!=ht_motion_game.camera || g->camera_y!=ht_motion_game.camera_y ||
        g->vista!=ht_motion_game.vista || g->rotation_phase!=ht_motion_game.rotation_phase ||
        g->sway_phase!=ht_motion_game.sway_phase || g->drop_zoom!=ht_motion_game.drop_zoom;
    ht_motion_game=*g;ht_motion_valid=true;return moving;
}
static bool ht_background_can_reuse(const ht_game *g) {
    const ht_game *old=&ht_background_game;
    return ht_retained_background && ht_background_valid && !ht_background_due &&
        g->level==old->level && g->deaths==old->deaths && g->laps==old->laps &&
        ht_abs(g->camera-old->camera)<=64*256 && ht_abs(g->camera_y-old->camera_y)<=64*256 &&
        ht_abs(g->x-old->x)<=64*256 && ht_abs(g->y-old->y)<=64*256 &&
        (g->level!=4 || (g->camera/256<=700)==(old->camera/256<=700));
}
static void ht_background_copy(uint8_t *dst,const uint8_t *src) {
    for(int y=0;y<HT_H;++y) {
        memcpy(dst+y*HT_W,src+y*HT_W,HT_W);
        if((y&15)==15)ht_checkpoint();
    }
}
