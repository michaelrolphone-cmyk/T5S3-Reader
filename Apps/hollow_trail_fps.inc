/* Rolling completed-frame FPS. At the app's >=42 ms frame-start interval,
 * at most 239 frames enter a 10-second window; 256 slots include boundary slack.
 * Reset on experiment changes/resume so paused time and other modes never mix. */
#define HT_FPS_WINDOW_MS 10000u
#define HT_FPS_SLOTS 256u
typedef struct {
    uint32_t stamp[HT_FPS_SLOTS],start,elapsed;
    unsigned head,count;
} ht_fps_window;
static void ht_fps_reset(ht_fps_window *w,uint32_t now) {
    w->start=now;w->elapsed=0;w->head=w->count=0;
}
static uint32_t ht_fps_push(ht_fps_window *w,uint32_t now) {
    while(w->count && now-w->stamp[w->head]>=HT_FPS_WINDOW_MS) {
        w->head=(w->head+1)%HT_FPS_SLOTS;--w->count;
    }
    if(w->count==HT_FPS_SLOTS) {w->head=(w->head+1)%HT_FPS_SLOTS;--w->count;}
    w->stamp[(w->head+w->count)%HT_FPS_SLOTS]=now;++w->count;
    if(w->elapsed<HT_FPS_WINDOW_MS) {
        uint32_t age=now-w->start;
        w->elapsed=age<HT_FPS_WINDOW_MS?age:HT_FPS_WINDOW_MS;
    }
    return w->elapsed?w->count*10000u/w->elapsed:0;
}
