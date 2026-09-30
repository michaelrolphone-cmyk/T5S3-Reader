/* Scene direction controls the character's pace and camera distance together.
 * Q8 speeds are world units per 32ms step. Mood never changes a jump in flight
 * or scales the simulation clock, input polling, weather or rigid bodies. */
#define HT_JUMP_SPEED 1520
#define HT_GRAVITY 112
static int ht_pace_target(const ht_game *g) {
    static const uint16_t walk[HT_LEVELS][5]={
        {272,288,352,304,272}, {272,288,336,288,272},
        {272,288,320,288,272}, {272,288,336,288,272},
        {272,288,304,288,272}, {272,288,320,288,272},
        {272,288,304,288,272}, {272,304,352,304,272},
        {256,272,304,272,256}, {272,288,304,272,256}
    };
    int x=ht_clamp(g->x/256,0,3199),span=x/800,u=x%800;
    int speed=walk[g->level][span]+((int)walk[g->level][span+1]-walk[g->level][span])*u/800;
    speed-=ht_exposure(g)/16;
    if(g->observation_ticks)speed-=24;
    return ht_clamp(speed,256,384);
}
static int ht_intimacy_target(const ht_game *g) {
    int close=g->level==4?0:256;
    if(g->observation_ticks)close=ht_min(256,close+80);
    const ht_mechanics *m=&ht_mechanics_by_level[g->level];
    if(m->rope_length) {
        int approach=ht_clamp(ht_abs(g->x/256-m->rope_anchor_x)-50,0,160);
        close=close*approach/160; /* Ease out before reaching the hanging rope. */
    }
    return close*(256-ht_vista_target(g))/256;
}
static int ht_scene_scale(const ht_game *g) {
    return 256+g->intimacy/2-g->vista*5/16;
}
static int ht_walk_speed(const ht_game *g) {return g->pace?g->pace:ht_pace_target(g);}
static int ht_climb_speed(const ht_game *g) {return ht_walk_speed(g)*3/4;}
