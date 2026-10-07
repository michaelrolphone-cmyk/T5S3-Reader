/* Chapter XI lives after the last ordinary route, not in the evidence/puzzle
 * arrays. A single bounded snapshot owns movement, physical pauses and the end.
 * No tick, input repeat, death retry or renderer can choose another testimony. */
enum {
    HT_DOOR_NONE,HT_DOOR_YARD,HT_DOOR_REST,HT_DOOR_STREET,HT_DOOR_SHOES,
    HT_DOOR_NOTEBOOK,HT_DOOR_GRIEF,HT_DOOR_STRAIGHTEN,HT_DOOR_KNOCK,HT_DOOR_WAIT
};
#define HT_DOOR_BARREL 266
#define HT_DOOR_SHOE_X 1123
#define HT_DOOR_STOP_X 1108
#define HT_DOOR_REST_TICKS 480u
#define HT_DOOR_SHOES_TICKS 240u
#define HT_DOOR_GRIEF_TICKS 480u
#define HT_DOOR_STRAIGHTEN_TICKS 240u
#define HT_DOOR_KNOCK_TICKS 160u
#define HT_DOOR_SCALE 384
#define HT_DOOR_CAMERA_Y 28
static int ht_door_ground(int x) {
    if(x<144)return 160+ht_clamp(x,0,144)/4; /* Worn far stair into paving. */
    return 196+ht_clamp(x-450,0,720)/36;
}
static bool ht_door_begin(void) {
    if(ht.door_stage || ht.level!=HT_LEVELS-1 || !ht.verdict || !ht.verdict_read ||
       !ht.puzzle.solved || ht.puzzle.opening!=48 || !ht.grounded || ht.x<HT_GOAL*256)return false;
    ht.door_stage=HT_DOOR_YARD;ht.door_tick=0;ht.door_notebook=false;
    ht.x=32*256;ht.y=ht_door_ground(32)*256;ht.vx=ht.vy=0;ht.facing=1;
    ht.camera=-120*256;ht.camera_y=HT_DOOR_CAMERA_Y*256;ht.stride=0;ht.grounded=true;
    memset(&ht.traversal,0,sizeof(ht.traversal));
    ht.sway_phase=ht.rotation_phase=ht.camera_mood=ht.vista=ht.intimacy=ht.drop_zoom=0;
    ht.weather_amount=ht.weather_age=ht.observation_ticks=0;ht.pace=184;
    return true;
}
static bool ht_door_interact(void) {
    if(ht.door_stage==HT_DOOR_YARD && ht_abs(ht.x/256-HT_DOOR_BARREL)<42) {
        ht.door_stage=HT_DOOR_REST;ht.door_tick=0;ht.x=HT_DOOR_BARREL*256;
    } else if(ht.door_stage==HT_DOOR_STREET && ht.x/256>=HT_DOOR_STOP_X-32) {
        ht.door_stage=HT_DOOR_SHOES;ht.door_tick=0;ht.x=HT_DOOR_STOP_X*256;
    } else return false;
    ht.y=ht_door_ground(ht.x/256)*256;ht.vx=ht.vy=0;ht.facing=1;
    return true;
}
static void ht_door_step(int direction) {
    if(ht.door_stage==HT_DOOR_WAIT)return; /* Held frame: no simulated answer. */
    ++ht.ticks;
    if(ht.door_stage==HT_DOOR_YARD || ht.door_stage==HT_DOOR_STREET) {
        int old=ht.x;
        /* Quiet walk and continuous foot contact; this street has no gaps or
         * jumps. The yard pause precedes the low passage, never a speed trigger. */
        int maximum=ht.door_stage==HT_DOOR_YARD?HT_DOOR_BARREL+30:HT_DOOR_STOP_X;
        ht.vx+=ht_clamp(direction*280-ht.vx,-48,48);
        ht.x=ht_clamp(ht.x+ht.vx,28*256,maximum*256);
        if(ht.x==old)ht.vx=0;
        if(direction)ht.facing=direction;
        ht.stride+=ht_abs(ht.x-old);ht.y=ht_door_ground(ht.x/256)*256;
    } else {
        ht.vx=ht.vy=0;
        if(ht.door_stage!=HT_DOOR_NOTEBOOK)++ht.door_tick;
        unsigned duration=ht.door_stage==HT_DOOR_REST?HT_DOOR_REST_TICKS:
            ht.door_stage==HT_DOOR_SHOES?HT_DOOR_SHOES_TICKS:
            ht.door_stage==HT_DOOR_GRIEF?HT_DOOR_GRIEF_TICKS:
            ht.door_stage==HT_DOOR_STRAIGHTEN?HT_DOOR_STRAIGHTEN_TICKS:
            ht.door_stage==HT_DOOR_KNOCK?HT_DOOR_KNOCK_TICKS:0;
        if(duration && ht.door_tick>=duration) {
            ++ht.door_stage;ht.door_tick=0;
            if(ht.door_stage==HT_DOOR_WAIT)++ht.laps; /* Complete only at the door. */
        }
    }
    /* Follow actual walking only. Every physical pause holds its composition;
     * neither shoe inspection nor the knock starts an automatic camera pull. */
    if(ht.door_stage==HT_DOOR_YARD || ht.door_stage==HT_DOOR_STREET) {
        int target=ht_clamp(ht.x/256-210,-120,HT_DOOR_STOP_X-210)*256;
        ht.camera+=(target-ht.camera)/8;
    }
    ht.camera_y=HT_DOOR_CAMERA_Y*256;
}
